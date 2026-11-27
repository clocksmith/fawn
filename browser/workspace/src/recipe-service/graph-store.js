// Both SQLite and D1 can commit an ordered batch atomically. Reduce a snapshot
// in memory, then compare its revision inside the same batch as the writes.
function putRow(table, row) {
  const columns = Object.keys(row);
  return {
    sql: `INSERT OR REPLACE INTO ${table} (${columns.join(', ')}) VALUES (${columns.map(() => '?').join(', ')})`,
    params: Object.values(row),
  };
}

function observeSegments(snapshot, meta, segments, authorHash, forcePromote, quorum, now) {
  const nodes = new Map(snapshot.nodes.map(row => [row.node_id, { ...row }]));
  const edges = new Map(snapshot.edges.map(row => [row.edge_key, { ...row }]));
  const authors = new Map(snapshot.authors.map(row => [row.edge_key, row]));
  const observed = new Set();
  function observeNode(id, outline, slots, clicks, depth, terminal, cleared) {
    let row = nodes.get(id);
    if (!row) {
      row = { namespace_id: meta.namespaceId, node_id: id, outline_hash: outline,
        outline_slots_json: JSON.stringify(slots), same_outline_clicks: clicks, depth, is_terminal: 0 };
      nodes.set(id, row);
    }
    if (cleared === id) row.is_terminal = 0;
    else if (terminal) row.is_terminal = 1;
  }
  for (const segment of segments) {
    for (const { id, next, depth, edgeKey, meta: m } of segment.records) {
      observeNode(id, m.beforeOutline, m.beforeSlots, m.beforeClicks, depth, false, segment.clearTerminal);
      observeNode(next, m.afterOutline, m.afterSlots, m.afterClicks, depth + 1,
        segment.terminal?.id === next, segment.clearTerminal);
      let edge = edges.get(edgeKey);
      if (!edge) {
        edge = { namespace_id: meta.namespaceId, edge_key: edgeKey, from_node: id, to_node: next,
          before_outline: m.beforeOutline, before_slots_json: JSON.stringify(m.beforeSlots), before_clicks: m.beforeClicks,
          after_outline: m.afterOutline, after_slots_json: JSON.stringify(m.afterSlots), after_clicks: m.afterClicks,
          step_kind: m.kind, locator_json: JSON.stringify(m.locator), slot_name: m.slot,
          attempts: 0, completions: 0, total_cost_ms: 0, promoted: 0 };
        edges.set(edgeKey, edge);
      }
      const prior = authors.get(edgeKey);
      edge.author_count = (prior?.total ?? 0) + (prior?.already_counted ? 0 : 1);
      edge.attempts++;
      edge.completions += Number(m.outcome === 'completed');
      edge.total_cost_ms += m.elapsedMs;
      edge.updated_at = now;
      if (forcePromote || meta.scope === 'workspace' || quorum <= 1 ||
          (edge.completions >= quorum && edge.author_count >= quorum)) edge.promoted = 1;
      observed.add(edgeKey);
    }
  }
  const authorWrites = [...observed].filter(key => !authors.get(key)?.already_counted).map(key => ({
    sql: 'INSERT INTO recipe_edge_authors (namespace_id, edge_key, author_hash, created_at) VALUES (?, ?, ?, ?)',
    params: [meta.namespaceId, key, authorHash, now],
  }));
  return { nodes, edges, observed, authorWrites };
}

function graphChanges(meta, snapshot, projected, reduced) {
  const statements = [...projected.authorWrites];
  const oldNodes = new Map(snapshot.nodes.map(row => [row.node_id, row]));
  for (const [id, node] of reduced.nodes) {
    const old = oldNodes.get(id);
    const terminal = Number(node.terminal);
    if (!old || old.depth !== node.depth || old.is_terminal !== terminal) {
      statements.push(putRow('recipe_nodes', { ...projected.nodes.get(id), depth: node.depth, is_terminal: terminal }));
    }
  }
  for (const id of oldNodes.keys()) {
    if (!reduced.nodes.has(id)) statements.push({
      sql: 'DELETE FROM recipe_nodes WHERE namespace_id = ? AND node_id = ?', params: [meta.namespaceId, id],
    });
  }
  for (const key of projected.observed) {
    if (reduced.survivingEdges.has(key)) statements.push(putRow('recipe_edges', projected.edges.get(key)));
  }
  for (const row of snapshot.edges) {
    if (!reduced.survivingEdges.has(row.edge_key)) statements.push({
      sql: 'DELETE FROM recipe_edges WHERE namespace_id = ? AND edge_key = ?', params: [meta.namespaceId, row.edge_key],
    });
  }
  return statements;
}

export function createRecipeGraphStore(store, reduceRows, quorum) {
  const revisionSql = 'SELECT revision FROM recipe_namespaces WHERE namespace_id = ?';
  async function snapshot(meta, { promotedOnly = false, edgeKeys, authorHash } = {}) {
    const params = [meta.namespaceId];
    const queries = [
      { sql: revisionSql, params },
      { sql: 'SELECT * FROM recipe_nodes WHERE namespace_id = ?', params },
      { sql: 'SELECT * FROM recipe_edges WHERE namespace_id = ?' + (promotedOnly ? ' AND promoted = 1' : ''), params },
    ];
    if (edgeKeys) queries.push({
      sql: `SELECT edge_key, COUNT(*) AS total, MAX(author_hash = ?) AS already_counted
            FROM recipe_edge_authors WHERE namespace_id = ? AND edge_key IN (SELECT value FROM json_each(?))
            GROUP BY edge_key`,
      params: [authorHash, meta.namespaceId, JSON.stringify(edgeKeys)],
    });
    const [namespace, nodes, edges, authors] = await store.batch(queries);
    return { revision: namespace[0]?.revision ?? 0, nodes, edges, authors };
  }
  return {
    async read(meta, limits) {
      const view = await snapshot(meta, { promotedOnly: meta.scope === 'catalog' });
      return { revision: view.revision, ...reduceRows(meta, limits, view.nodes, view.edges) };
    },
    async commit(meta, limits, segments, { authorHash, forcePromote, promotedSummary, priorResponse, finish }) {
      const edgeKeys = [...new Set(segments.flatMap(seg => seg.records.map(record => record.edgeKey)))];
      for (let attempt = 0; attempt < 8; attempt++) {
        const view = await snapshot(meta, { edgeKeys, authorHash });
        const prior = await priorResponse?.();
        if (prior) return prior;
        const now = new Date().toISOString();
        const graph = observeSegments(view, meta, segments, authorHash, forcePromote, quorum, now);
        const reduced = reduceRows(meta, limits, [...graph.nodes.values()], [...graph.edges.values()]);
        const statements = graphChanges(meta, view, graph, reduced);
        // revision is NOT NULL. A concurrent writer makes this statement fail,
        // rolling back the entire batch, including any observations and receipt.
        statements.unshift({
          sql: `INSERT INTO recipe_namespaces
                (namespace_id, scope_kind, workspace_id, origin, template, read_scope, dag_key, revision, updated_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, 1, ?)
                ON CONFLICT(namespace_id) DO UPDATE SET updated_at = excluded.updated_at,
                  revision = CASE WHEN revision = ? THEN revision + 1 END`,
          params: [meta.namespaceId, meta.scope, meta.workspaceOwner, meta.origin, meta.template,
            meta.readScope, meta.dagKey, now, view.revision],
        });
        // Catalog composition reports only the promoted graph visible to its caller.
        const summary = promotedSummary
          ? reduceRows(meta, limits,
            [...graph.nodes.values()].filter(row => reduced.nodes.has(row.node_id)),
            [...graph.edges.values()].filter(row => row.promoted && reduced.survivingEdges.has(row.edge_key)))
          : reduced;
        const response = finish(view.revision + 1, summary, statements);
        try {
          await store.batch(statements);
          return response;
        } catch (error) {
          const receipt = await priorResponse?.();
          if (receipt) return receipt;
          const current = await store.get(revisionSql, [meta.namespaceId]);
          if ((current?.revision ?? 0) === view.revision) throw error;
        }
      }
      throw Object.assign(Error('Concurrent graph updates; retry this request'), { status: 503, code: 'graph-busy' });
    },
  };
}
