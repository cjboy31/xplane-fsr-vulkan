# Share a diagnostic report

A report helps the community understand what happened during your flight.
Reporting starts **Off**. Opening the report window does not upload anything.

1. In X-Plane, open **Plugins > FSR 3.1 / 4.1 + Frame Gen > Open settings**.
2. Open **Diagnostics**, scroll down, then choose **Preview / send a Discord report**.
3. Choose **#render-logs** to share a log, or **#bug-reports** to explain a problem.
4. If needed, select **Also include Log.txt and available support diagnostics**.
   The preview lists every included text file. Read it before sharing.
5. Add a short description, check the sharing agreement, then choose:
   - **Send report now** for the current snapshot.
   - **Send after this session** for one automatic report when X-Plane closes.

Use **Cancel automatic report** before exiting to turn the queued report Off.
Automatic reporting applies to this session only. After an unexpected exit, the
background helper can still send the report if it is running and the session log
was saved. A PC shutdown or lost connection can prevent delivery.

The panel shows the result. **Sent** means Discord returned a receipt for the
selected channel. If delivery is uncertain, check the channel before trying
again. The automatic report is attempted once, without repeated uploads.

## What can be included?

- The renderer log from this session.
- X-Plane's **Log.txt**, when it can be matched to the session.
- A short summary of saved image settings and the mod version.
- Available startup/setup error text. It is labeled if it may predate the flight.
- Up to three small crash text files from this session, when present.
- Saved numeric Native AA options, when available. These do not prove Native AA ran.

Large logs are shortened for upload. Older or unverified Log.txt files are left
out. Binary crash dumps, DLLs, models, aircraft/scenery files, original preference
files, controller bindings and unrelated documents are excluded.

The mod filters common folder paths, emails, links, location fields and passwords.
**Filtering cannot find every private detail.** Check the preview. People who can
read the chosen Discord channel can read the report and its attachments.

## Where is my copy?

Filtered bundles are saved inside your X-Plane folder under
**Output > FSR-Logs > reports**. Original logs stay unchanged. Choose
**Save bundle / website** to save a filtered copy and open the upload page.

The [report website](https://xplane-mod-log-reports.cjboy31.chatgpt.site/) also
accepts Native AA logs and works in a browser on Windows, Linux and Mac. Enter
the version shown in your plugin panel. Browser upload does not add rendering
support on an unsupported platform. Automatic in-sim submission currently uses
the Windows FSR support helper; the Linux rendering preview is unchanged.

## Service limits

The service accepts four reports per reporting connection each day. New
connections are limited to three per public IP each day, with a service-wide
limit. Users sharing a network can reach that connection limit together. Keep
your local copy if a limit is reached. The service stores connection hashes,
limits and delivery receipts. Discord stores the uploaded text attachments.
The app never contains a Discord posting credential.
