# Review of reported issues

This review covers the supplied transcript of [the video](https://www.youtube.com/watch?v=yC9pkuq6O8g), compared with the current working tree. A report in the video is evidence of the reported experience, not proof that every current build has the same cause.

| Report | Assessment and action |
| --- | --- |
| Inventory and map can freeze, including during the Loftwing quest. | A valid stability report. The exact scenes have not been reproduced with the available save. Input sampling now advances on a steady clock instead of on each state read. Windows memory commitment now handles concurrent first accesses, and failed host thread creation is returned as an error. These fixes have regression tests, but do not establish that the reported menu freezes are fixed. |
| Mia has the wrong daytime face; trees near the captive Loftwing appear blue or purple. | Specific rendering reports requiring matching scene captures. Neither has been verified in the current build. No speculative texture or color replacement has been applied. |
| The extractor contains a fixed decryption key and image-specific offsets. | Confirmed in the old script. The script has been retired and replaced with an actionable compatibility notice. Setup instructions now describe importing prepared ExeFS/RomFS files. No encrypted image or key import is advertised. |
| Executables, libraries and extraction tools are committed with the source. | Confirmed. The current Git index removes the root runtime binaries and obsolete extraction tools. Local runtime files are preserved for playing; tools were moved to an ignored backup. Ignore rules and a source-check workflow prevent their accidental reintroduction. Historical commits and previous releases have not been rewritten or removed. |
| LASRecomp attribution was missing. | Attribution and its license are present in the current source. Packaging now also includes that license and the ImGui/SMAA notices; Windows and macOS font packaging paths were corrected. This does not establish what an older downloaded build contained. |
| The launcher resembles LASRecomp and shares code. | Reuse is observable and should remain clearly credited. Similarity alone does not establish a defect or plagiarism. Existing attribution is retained; no cosmetic rewrite was made to conceal reuse. |
| SDL3 is an ImGui library. | Imprecise: SDL supplies platform facilities such as windowing, input and audio; ImGui supplies the interface. Shipping SDL is not itself evidence of an error. |
| A 32-digit hexadecimal key is eight bytes. | Incorrect: 32 hexadecimal digits encode 16 bytes, or 128 bits. That correction does not excuse a fixed key in the retired extractor. |
| Frame rate drops and the project is presented as more stable than it is. | Full-game stability and a constant frame rate have not been demonstrated. The README now calls the project experimental and removes the constant-60-FPS claim. 120 FPS remains experimental. |
| AI use, maintainer count and a recommendation against playing. | Opinions and process criticism cannot be verified as code defects from the transcript. The actionable response is reproducible bug reports, accurate status, tests and preserved attribution. |

## Diagnostics and validation

Normal Windows launches now open a separate console following `skyrecomp.log`. A watchdog records possible freezes after 15 seconds without a presented frame, including guest thread states when the kernel lock is available. Closing the console does not close the game. See [diagnostic instructions](diagnostics.md); long loading can also produce a possible-freeze report.

Checks cover concurrent guest memory commitment, protected/execute fault rejection, failed thread creation, stable input sampling, automatic freeze reporting and its busy-lock path, console launch arguments, and current source hygiene. The Windows executable has been rebuilt. Existing control, settings and graphics regression checks also pass.

The reported inventory/map scenes, Mia and the Loftwing-area trees still need matching runtime evidence. Full-game stability, macOS and Linux execution have not been validated by this review. Removing material from the current index does not remove it from Git history, previous releases or existing downloads; that cleanup requires a separate distribution/history decision.
