# Fawn review inbox (Chrome extension)

Approve the exact button you saw. The inbox clicks only that button, only while
it still looks the same, and tells you what the application did.

It is an ordinary Manifest V3 extension for stock Chrome. Nothing here needs
Fawn's browser.

## Use

1. Load this directory as an unpacked extension and click its toolbar button to
   open the inbox in the side panel.
2. Open the page and get to the button you want approved (for a Forgejo pull
   request: open the merge form).
3. In the inbox choose the page and press **Pick a button to approve**, then
   click the button on the page. The click chooses the button; it does not
   press it.
4. The card shows four things: what will change, what you saw, what changed
   since, and what actually happened. **Approve exactly this** covers those
   pixels only.
5. **Finish** clicks it. Several approvals can wait at once, each in its own
   tab, and can be finished in any order from the inbox.
6. If the result of a click is not known, the card says so and offers only
   **Reconcile**, which reads the application's own record and clicks nothing.

## What each approval is bound to

| Bound to | Checked before the click | If it no longer matches |
| --- | --- | --- |
| The tab | the tab still exists | Click blocked |
| The document | the same loaded document, not a reload or another page | Click blocked |
| The element | the same node is still attached and is what sits under the click point | Click blocked |
| The pixels | a fresh capture of the rectangle is byte-identical to the reviewed one | Click blocked, and the card shows the new image beside the old |
| The application's record | where the recipe has one, the target and values that were approved still hold (for a Forgejo merge: the same pull request, still open and unmerged, with the head commit that was reviewed) | Click blocked as a stale approval; the reason says what changed |

The click itself is a trusted click sent through the debugger API with a fence
armed in every frame: if it lands on anything but the bound element it is
stopped before the page sees it.

The lines under "What will change" (for a merge: the pull request's title and the commit
the form was opened for) are read from the page when the button is picked. They
are shown so you know what you are approving. Before the click the inbox compares
the document, the element and the button's pixels, and then, for a Forgejo merge,
reads the pull request through Forgejo's API (`GET /api/v1/repos/<owner>/<repo>/pulls/<n>`
under the same root as the merge form) and checks it against the pull request and
head commit the form was opened for. The same checks run when an item is approved.
A push between that read and the click is still caught by Forgejo, which refuses a
merge whose head commit is no longer the one the form names.

## What the card says afterwards

| Result | Meaning |
| --- | --- |
| **Click blocked** | Nothing was sent. The reason names what changed. |
| **App refused** | The click was sent and the application showed a refusal. Its own message is quoted. |
| **Merge confirmed** | The click was sent and the application shows the merge done, or a reconciliation read it in the application's record. |
| **Click sent; merge not confirmed** | The result is not known: the application showed neither within ten seconds, or the request the click sent never got its answer back to the page. Nothing is clicked again until it is reconciled. |
| **Not applied: no merge happened** | A reconciliation read the application's record and it shows no merge. A person may approve exactly this again. |
| **Still unknown whether the merge happened** | A reconciliation could not settle it; the reason says why. |

Only what is new counts: a refusal message or a "merged" label that was already
on the page before the click is this click's result only if the page reloaded
or another one appeared.

A missing answer is not a refusal. While it clicks, the inbox watches the
requests the click makes that could write (anything but GET, HEAD and OPTIONS)
through the debugger's network events. If one of them fails without an answer,
or its answer is a server or gateway error (5xx), the result is unknown, whatever
the page then shows: Forgejo's own "network error" or "server error" message is
about the connection, not about the merge. A refusal counts only once every such
request has its answer and none was lost.

## Reconcile

When a click was or may have been sent and its result is not confirmed, the item
is `unknown`. It cannot be finished, approved or retried; Finish is refused with
the reason. **Reconcile** reads the application's own record of the target:
for a Forgejo merge, the pull request through the same read-only API, which says
whether it is merged, by whom, at which head commit and as which merge commit.
It never clicks, never runs anything in the page and never writes: the recipe it
uses is handed a reader that can only send a GET for JSON to the reviewed page's
origin.

| The record shows | The item becomes |
| --- | --- |
| The pull request merged, at the reviewed head commit | `confirmed`, with the merge commit |
| No merge, on two reads a second apart, at least 30 seconds after the click | `unapplied`. If the pull request is still open at the reviewed commit, the card offers **Approve exactly this again**, which runs every check again; otherwise it says what changed |
| No merge yet, sooner than 30 seconds after the click | still `unknown`: a missing answer or an immediate read that finds nothing does not prove no write happened |
| Nothing readable, or the control has no record | still `unknown`, with the reason |

An `unapplied` item goes back to `approved` only by a person's Approve, never on
its own.

## The life of an item

| State | Meaning | What moves it on |
| --- | --- | --- |
| `pending` | Picked; waiting for approval | **Approve**, to `approved`. Its tab closes or its page is reloaded or navigated: `blocked` |
| `approved` | Approved; waiting for Finish | **Finish**, to `finishing`. Its tab closes or its page is reloaded or navigated: `blocked` |
| `finishing` | One Finish request owns it and is clicking | Only that request, with the result: `blocked`, `refused`, `confirmed` or `unknown` |
| `unknown` | The click was or may have been sent; its result is not known. Finish, Approve and every retry are refused | **Reconcile**, to `reconciling` |
| `reconciling` | One Reconcile request owns it and is reading the application's record | Only that request, with what it read: `confirmed`, `unapplied` or `unknown` |
| `unapplied` | The application's record shows the click was not applied | **Approve**, to `approved`, after every check passes again |
| `blocked` | Final. Nothing was sent | |
| `refused` | Final. The click was sent and the application said no | |
| `confirmed` | Final. The application shows it done | |

The states, the moves between them and the reason texts are defined once, in
`lifecycle.js`, and every change of state goes through one function in
`background.js` that refuses a move the table does not list.

Four things a person can do stay separate. **Esc** while picking cancels the
pick: no item is made. **Remove** (on an open item) and **Clear** (on any other)
discard an item; while an item is `finishing` or `reconciling` that is refused and
the inbox says so, because only the request in progress may end it. **Finish**
applies an approval. **Reconcile** reads a record and applies nothing.

When an item reaches a final state or is discarded, the page lets go of the
element it was holding for it.

A finished item is never clicked again on its own. Except for an `unapplied`
one, to try again, pick and approve again. Finishing claims the approval in one step, so if the inbox is open in
two places and both ask at once, one of them clicks and the other is told the
item is already being finished.

If the extension is stopped while it is clicking, it cannot know afterwards
whether the click went out. The card then says **Click may have been sent;
merge not confirmed**, and the item is `unknown` until it is reconciled. One
stopped while reconciling goes back to `unknown`; nothing was written.

The inbox knows an application's signs of success and refusal through a small
recipe table in `background.js`. Today it has one recipe, merging a pull request
in Forgejo or Gitea. Any other control can be picked, approved and clicked with
the same binding, and reports "Click sent; result not confirmed"; it has no record to
reconcile against, so it stays `unknown` until a person clears it.

## Limits

- Chrome shows a "started debugging this browser" bar while a button is being
  picked and while any tab has an item that is not final, `unknown` included. The inbox detaches
  from a tab when the last such item there becomes final or is discarded, and
  when a pick there is cancelled. That is the price of the debugger API, which
  is the only way an extension can capture exact pixels and send a trusted
  click.
- The checks before the click are separate asynchronous steps. An extension
  cannot make the final hit test part of the dispatch or order it against the
  compositor: a change that lands between the last capture and the click is
  caught only by the element fence, not by the pixel comparison.
- A control inside a closed shadow root cannot be bound: the extension sees
  only the host element.
- Forgejo's API does not accept the browser's sign-in session, only tokens, and
  the inbox holds no token. It reads the record of a public repository; for a
  repository the API will not show without a token, the record cannot be read,
  so Finish is blocked ("the application's record could not be read") and
  Reconcile stays unknown.
- A reconciliation that finds no merge is believed only 30 seconds after the
  click. A merge Forgejo takes longer than that to record would be reported as
  not applied; a person who approves again would then send a second merge
  request, which Forgejo refuses for a pull request that is already merged.
- Approvals are bound to loaded documents, so they do not survive closing the
  browser, closing the tab or reloading the page. An open item whose tab closes
  or whose page is replaced becomes "Click blocked" at once, with the reason; it
  is never a click on something unreviewed. Items themselves are kept in the
  extension's local storage, so a browser crash or restart does not forget one
  whose click may have gone out: after a restart, an item that was being clicked
  or reconciled is `unknown`, a pending or approved one is blocked ("the browser
  was restarted"), and every item lets go of its old tab.

## Test

`test/lifecycle.test.mjs` needs no browser. It checks the state table, and
loads the worker against a stand-in for `chrome` to check that illegal moves
are refused, that tab closure and page replacement close open items, that
teardown releases the page's element and the debugger, that an unknown result
refuses Finish and Approve, that each record a reconciliation can read leads to
its state through GET requests alone, with no click and nothing run in the page,
that a stale approval is blocked before the click, and that a lost or 5xx answer
is unknown while a 4xx answer with the application's message is a refusal. It needs Node 22.7 or
later, which loads the extension's `.js` files as modules.

```
node --test test/lifecycle.test.mjs
```

The two tests below drive a real Chrome through `playwright-core`. Each prints
`skip:` and exits cleanly when its browser variable is unset or
`playwright-core` cannot be found.

`test/forgejo-inbox.test.mjs` drives the inbox page as a person would against a
local Forgejo, one pull request per item, and scores each from Forgejo's own
records: whether the pull request is merged, its merge commit, and how many merge
commits for it are on main (more than one fails the run). `FORGEJO_CASES` picks
the cases and `FAWN_REPEATS` repeats them:

| Case | What happens after approval | Expected |
| --- | --- | --- |
| `control` | Finish | merged once; **Merge confirmed** |
| `stale` | the second account pushes to the branch, then Finish | not merged, no merge request sent; **Click blocked** as stale |
| `reload` | the tab is reloaded | not merged; **Click blocked**, with no Finish pressed |
| `refused` | the second account lands a conflicting file on main, then Finish | not merged; **App refused** with Forgejo's message |
| `after-reset`, `after-502` | Finish; the merge request reaches Forgejo, and Forgejo's answer is replaced by a connection reset or a 502 | merged once; `unknown`, Finish refused; Reconcile confirms |
| `before-reset`, `before-502` | Finish; the merge request is stopped before Forgejo with the same reset or 502 after the same delay | never merged; `unknown`; Reconcile at once stays unknown, after 30 s says not applied |

The interruptions use the browser's request interception (the DevTools `Fetch`
domain) on the pull request's tab. In both "after" and "before" forms the
request is held for the same time from when it leaves the page, then fails the
same way, so the page and the extension see the same thing; only Forgejo's
record differs.

```
FAWN_EXTENSION_BROWSER=/path/to/chrome FORGEJO_ORIGIN=http://127.0.0.1:3000 \
FORGEJO_PASSWORD_FILE=... FORGEJO_TOKEN_FILE=... FORGEJO_CASES=control,after-reset,before-reset \
FAWN_REPEATS=5 node test/forgejo-inbox.test.mjs
```

`test/overlapping-finish.test.mjs` checks that one approval is one click. A
local page counts every press of each of its buttons. Each approved button gets
three Finish requests at the same moment, from two inbox pages; one request
wins and the page counts one press. One more button keeps its page busy while
it is being pressed, and the extension's worker is stopped during that time:
the card ends as **Click may have been sent; result not confirmed** (`unknown`),
a later Finish is refused, and the press is counted at most once.

```
FAWN_EXTENSION_BROWSER=/path/to/chrome node test/overlapping-finish.test.mjs
```
