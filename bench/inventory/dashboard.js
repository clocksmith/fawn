// Presentation only: selections and qualifications come from the Python owner.
(() => {
  const rows = JSON.parse(document.getElementById('inventory-data').textContent);
  const ids = ['os', 'machine', 'section', 'family', 'workload', 'metric', 'build', 'level', 'status'];
  const controls = Object.fromEntries(ids.map(id => [id, document.getElementById(id)]));
  const results = document.getElementById('results');
  const limit = 100;
  let page = 0;
  function matches(row) {
    for (const id of ['os', 'section', 'family']) {
      if (controls[id].value && row[id] !== controls[id].value) return false;
    }
    const machine = row.machine.startsWith('unknown:') ? 'unknown' : row.machine;
    if (controls.machine.value && controls.machine.value !== machine) return false;
    if (controls.level.value === 'aggregate' && row.level === 'execution') return false;
    if (controls.level.value === 'execution' && row.level !== 'execution') return false;
    if (controls.status.value === 'failed' && row.status !== 'failed') return false;
    if (controls.status.value === 'qualified' && !row.qualified) return false;
    const includes = (value, query) => String(value).toLowerCase().includes(query.toLowerCase());
    if (!includes(row.workload, controls.workload.value)) return false;
    if (!includes(row.build + ' ' + row.comparator, controls.build.value)) return false;
    return !controls.metric.value || row.metrics.some(m => includes(m.name, controls.metric.value));
  }
  function cell(tr, value) {
    const td = document.createElement('td');
    td.textContent = value;
    tr.appendChild(td);
    return td;
  }
  function render() {
    const filtered = rows.filter(matches);
    page = Math.min(page, Math.max(0, Math.ceil(filtered.length / limit) - 1));
    results.replaceChildren();
    for (const row of filtered.slice(page * limit, (page + 1) * limit)) {
      const tr = document.createElement('tr');
      const title = cell(tr, row.workload + ' / ' + row.familyLabel + ' ');
      const link = document.createElement('a');
      link.href = row.history;
      link.textContent = 'Evidence and history';
      title.appendChild(link);
      const identity = document.createElement('small');
      identity.textContent = ' · observation ' + row.id.slice(0, 12);
      identity.title = row.id;
      title.appendChild(identity);
      cell(tr, row.machine + ' / ' + row.recordedOs + ' / ' + (row.backend ?? 'unknown'));
      const buildText = row.build + ' vs ' + row.comparator;
      const buildCell = cell(tr, buildText.replace(/\b[a-f0-9]{40,64}\b/g, hash => hash.slice(0, 12)));
      buildCell.title = buildText;
      cell(tr, (row.date ?? 'unknown') + ' / ' + (row.freeze ?? 'unknown'));
      const metrics = cell(tr, '');
      const list = document.createElement('ul');
      list.className = 'metric-list';
      for (const metric of row.metrics.filter(m => m.name.toLowerCase().includes(controls.metric.value.toLowerCase()))) {
        const item = document.createElement('li');
        const value = n => n == null ? 'unknown' : typeof n === 'number' ? Number(n.toPrecision(5)) : n;
        item.textContent = `${metric.name}: ${value(metric.implementationValue)} / ${value(metric.comparatorValue)} ${metric.unit ?? 'unknown'}; ${metric.outcome}`;
        list.appendChild(item);
      }
      if (!list.childNodes.length) metrics.textContent = 'No measurement: ' + row.verdict;
      else if (controls.metric.value) metrics.appendChild(list);
      else {
        const details = document.createElement('details');
        const summary = document.createElement('summary');
        const wins = row.metrics.filter(m => m.outcome === 'win').length;
        const losses = row.metrics.filter(m => m.outcome === 'loss').length;
        summary.textContent = `${wins} win / ${losses} loss / ${row.metrics.length - wins - losses} other metrics`;
        details.append(summary, list);
        metrics.appendChild(details);
      }
      const selectedLabel = id => id === row.id ? 'this observation' : id ? id.slice(0, 12) : 'unknown';
      const selectionCell = cell(tr, [row.status, row.qualified ? 'qualified' : 'unqualified',
        row.currentSourceMeasured ? 'current source measured subset' : 'current source unmeasured',
        'latest measured: ' + selectedLabel(row.latestMeasured),
        'latest qualified: ' + selectedLabel(row.latestQualified)].join('; '));
      selectionCell.title = 'Latest measured: ' + (row.latestMeasured ?? 'unknown') + '\nLatest qualified: ' + (row.latestQualified ?? 'unknown');
      results.appendChild(tr);
    }
    document.getElementById('count').textContent = `${filtered.length.toLocaleString()} matching / ${rows.length.toLocaleString()} total observations`;
    document.getElementById('page').textContent = `Page ${page + 1} of ${Math.max(1, Math.ceil(filtered.length / limit))}`;
    document.getElementById('previous').disabled = page === 0;
    document.getElementById('next').disabled = (page + 1) * limit >= filtered.length;
  }
  ids.forEach(id => controls[id].addEventListener('input', () => { page = 0; render(); }));
  document.getElementById('previous').addEventListener('click', () => { --page; render(); });
  document.getElementById('next').addEventListener('click', () => { ++page; render(); });
  document.getElementById('reset').addEventListener('click', () => {
    ids.forEach(id => { controls[id].value = id === 'level' ? 'aggregate' : ''; });
    page = 0; render();
  });
  render();
})();
