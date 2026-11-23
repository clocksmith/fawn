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

## What each approval is bound to

| Bound to | Checked before the click | If it no longer matches |
| --- | --- | --- |
| The tab | the tab still exists | Click blocked |
| The document | the same loaded document, not a reload or another page | Click blocked |
| The element | the same node is still attached and is what sits under the click point | Click blocked |
| The pixels | a fresh capture of the rectangle is byte-identical to the reviewed one | Click blocked, and the card shows the new image beside the old |

The click itself is a trusted click sent through the debugger API with a fence
armed in every frame: if it lands on anything but the bound element it is
stopped before the page sees it.

The lines under "What will change" (for a merge: the pull request's title and the commit
the form was opened for) are read from the page when the button is picked. They
are shown so you know what you are approving; what is compared before the click
is the document, the element and the button's pixels.

## What the card says afterwards

| Result | Meaning |
| --- | --- |
| **Click blocked** | Nothing was sent. The reason names what changed. |
| **App refused** | The click was sent and the application showed a refusal. Its own message is quoted. |
| **Merge confirmed** | The click was sent and the application shows the merge done. |
| **Click sent; merge not confirmed** | The click was sent and the application showed neither within ten seconds. Nothing is clicked again. |

Only what is new counts: a refusal message or a "merged" label that was already
on the page before the click is this click's result only if the page reloaded
or another one appeared.

A finished item is closed whatever happened. To try again, pick and approve
again. Finishing claims the approval in one step, so if the inbox is open in
two places and both ask at once, one of them clicks and the other is told the
item is already being finished.

If the extension is stopped while it is clicking, it cannot know afterwards
whether the click went out. The card then says **Click may have been sent;
merge not confirmed**, and the item stays closed.

The inbox knows an application's signs of success and refusal through a small
recipe table in `background.js`. Today it has one recipe, merging a pull request
in Forgejo or Gitea. Any other control can be picked, approved and clicked with
the same binding, and reports "Click sent; result not confirmed".

## Limits

- Chrome shows a "started debugging this browser" bar while the inbox holds any
  approval. That is the price of the debugger API, which is the only way an
  extension can capture exact pixels and send a trusted click.
- The checks before the click are separate asynchronous steps. An extension
  cannot make the final hit test part of the dispatch or order it against the
  compositor: a change that lands between the last capture and the click is
  caught only by the element fence, not by the pixel comparison.
- A control inside a closed shadow root cannot be bound: the extension sees
  only the host element.
- Approvals live for the browser session and are bound to loaded documents, so
  they do not survive closing the browser or reloading the page. Both end as
  "Click blocked", never as a click on something unreviewed.

## Test

`test/forgejo-inbox.test.mjs` drives the inbox page as a person would against a
local Forgejo: three pull requests in three tabs are picked and approved, then
finished out of order, one ending in each of **Merge confirmed**, **App
refused** and **Click blocked**.

```
FAWN_EXTENSION_BROWSER=/path/to/chrome FORGEJO_ORIGIN=http://127.0.0.1:3000 \
FORGEJO_PASSWORD_FILE=... FORGEJO_TOKEN_FILE=... node test/forgejo-inbox.test.mjs
```

`test/overlapping-finish.test.mjs` checks that one approval is one click. A
local page counts every press of each of its buttons. Each approved button gets
three Finish requests at the same moment, from two inbox pages; one request
wins and the page counts one press. One more button keeps its page busy while
it is being pressed, and the extension's worker is stopped during that time:
the card ends as **Click may have been sent; result not confirmed**, a later
Finish is refused, and the press is counted at most once.

```
FAWN_EXTENSION_BROWSER=/path/to/chrome node test/overlapping-finish.test.mjs
```
