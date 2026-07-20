# Steam release-readiness checklist

This checklist tracks the free Linux x86_64 Steam edition. A checked engineering
item means it is reproducible in this repository; it is not a statement that
Valve or legal review has approved the release.

## Complete in the repository

- [x] Steam code path is separate from the Raspberry Pi repository.
- [x] Steam self-update and Pi-only service, fan, SSH, and Bluetooth controls
  are disabled or hidden.
- [x] PC Link uses a depot-local Moonlight executable when bundled.
- [x] Game Center uses depot-local RetroArch and cores when bundled.
- [x] RetroNAS supports a read-only rclone FUSE mount plus a no-FUSE,
  selected-game download cache.
- [x] RetroNAS credentials use rclone's obscured form and a mode-0600 config.
- [x] The companion HTTP service is off by default and binds to localhost when
  a Steam user opts in.
- [x] The application builds and its automated tests pass in Valve's Steam
  Linux Runtime 3.0 (sniper) SDK.
- [x] The portable depot validator checks architecture, shared libraries,
  runtime notices, approved cores, credentials, and accidental ROM/BIOS files.
- [x] Steamworks ContentBuilder templates are kept separate from the shipped
  depot.
- [x] The depot identifies its exact source revision and independent source
  repository.
- [x] Preview-mode Steamworks manifests can be generated from numeric App and
  Depot IDs without modifying or publishing the checked-in templates.
- [x] The development depot passes the normal runtime, architecture,
  dependency, credential, and content-file validation gates.
- [x] Conservative store-page copy, system requirements, screenshot alt text,
  and Steamworks feature selections are drafted.

## Can be completed before Steam IDs are assigned

- [x] Build and test distributable mpv, Moonlight, and RetroArch runtime
  bundles in the sniper SDK.
- [x] Select a minimal candidate core set and record each exact upstream source,
  revision, license, and redistributable dependencies.
- [ ] Obtain the final rights approval for each core before listing it in
  `APPROVED-CORES.txt`.
- [x] Produce and validate all required Steam store and library artwork.
- [x] Capture and review at least five 1920x1080 screenshots from the real
  application.
- [x] Capture and technically validate a real-application 1080p Steam trailer
  draft.
- [ ] Approve the final trailer edit and audio choice before upload.
- [ ] Exercise controller-only navigation and text entry on a Linux desktop.
- [ ] Test RetroNAS guest and authenticated SMB in both FUSE and cache modes.
- [ ] Test PC Link against a Sunshine host.
- [ ] Test Game Center launch/return with user-supplied legal ROMs and BIOS.
- [ ] Test suspend/resume, network loss, reconnect, display scaling, and audio
  device changes.

## Requires Steamworks values or Valve access

- [x] Steam Direct fee paid, as confirmed by the project owner.
- [ ] Confirm the applicable 30-day post-fee waiting period has elapsed.
- [ ] Replace `YOUR_APP_ID` and `YOUR_LINUX_DEPOT_ID` by generating private
  ContentBuilder manifests from the templates.
- [ ] Configure the Linux launch option as `./launch-tater-tube.sh`.
- [ ] Upload to a private Steam branch with ContentBuilder preview mode first.
- [ ] Complete the store-page review before submitting the build review.
- [ ] Publish the approved Coming Soon page for at least two weeks before
  release.
- [ ] Verify the build from a clean desktop Linux Steam client.
- [ ] Verify the build in Steam Deck Gaming Mode with controller-only input.
- [ ] Complete Valve's release checklist and release-date waiting periods.

## Release blockers

The public build must not be submitted until all of these are resolved:

1. The strict depot validator passes with every runtime and notice bundled.
2. `APPROVED-CORES.txt` names only cores approved for this exact distribution.
3. The license/source inventory covers the application, Qt, every runtime,
   every core, and their shipped dependencies.
4. The Steam Distribution Agreement and the GPL/dependency combination have
   received the project's final rights review.
5. The store page advertises only features that pass on the uploaded build.
6. No credentials, private configuration, ROMs, BIOS files, disc images, or
   Steamworks SDK redistributables are present in the depot.

The current development depot passes normal validation. Strict validation is
deliberately stopped by exactly three core-approval gates: no approved core,
no `retroarch-cores.txt`, and no `APPROVED-CORES.txt`.

The application intentionally does not link the Steamworks SDK and does not use
Steam DRM. Adding Steam Input, Rich Presence, achievements, DRM, or another
SDK-linked feature requires a new compatibility review.
