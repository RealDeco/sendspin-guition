# Example Automations

A collection of ready-to-use Home Assistant automations for devices in this repository.
Import any of these via **Settings → Automations → Create Automation → Edit in YAML**, paste
the file's contents, and adjust the entity IDs to match your own devices.

## cycle_family_photo.yaml

Cycles the "Family Photo" clock style's background image on a schedule. Every 5 minutes it
picks a random photo from a hardcoded list and pushes its URL to one or more devices' `family
photo url` text entity, which the device then downloads and displays.

- **Trigger:** every 5 minutes (`time_pattern`, `minutes: /5`).
- **Photo source:** files served by Home Assistant's local `www` folder at
  `http://homeassistant.local:8123/local/family_photos/`. Add or remove entries in the
  `photos` list to control which images are eligible.
- **Targets:** any number of `text.*_family_photo_url` entities can be listed under
  `entity_id` — all listed devices are updated with the same chosen photo at the same time.

To add a new photo: drop the image file (480x480 PNG recommended) into
`/config/www/family_photos/` using the File Editor add-on, then add its filename to the
`photos` list in the automation.
