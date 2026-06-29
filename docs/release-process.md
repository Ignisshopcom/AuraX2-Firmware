# Release Process

Use this process whenever a firmware version should be visible in GitHub
Releases and traceable later.

## Versioning

Use date-based version tags unless the project intentionally switches to
semantic versioning:

- `YY-MM-DD`
- Example: `26-05-20`

If more than one release is made on the same day, add a suffix:

- `26-05-20-2`

## Before Release

1. Make sure the working tree contains only intended changes:

   ```bash
   git status
   ```

2. Build the firmware:

   ```bash
   pio run
   ```

3. Update `CHANGELOG.md`:

   - Move items from `Unreleased` into a new version section.
   - Add the release date.
   - Keep the newest version at the top.

## Create Release

1. Commit the release notes:

   ```bash
   git add CHANGELOG.md
   git commit -m "docs: prepare 26-05-20 release"
   ```

2. Create and push a tag:

   ```bash
   git tag -a 26-05-20 -m "AuraX2 26-05-20"
   git push origin master
   git push origin 26-05-20
   ```

3. Open GitHub Releases for `Ignisshopcom/AuraX2-Firmware` and create a release from the tag.
   GitHub will use `.github/release.yml` to group generated release notes.

## Commit Style

Short conventional commit prefixes make release notes easier to read:

- `feat:` new user-visible behavior
- `fix:` bug fix
- `docs:` documentation only
- `chore:` maintenance
- `refactor:` internal code cleanup without behavior change
