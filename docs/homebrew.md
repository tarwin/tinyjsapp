# Homebrew (custom tap)

The formula in [`packaging/homebrew/Formula/tinyjs.rb`](../packaging/homebrew/Formula/tinyjs.rb)
installs the published tinyjs release, including its runtime, launcher, templates,
and CLI. It does not compile tinyjs or run the website's installation script.

This is a formula for a **custom tap**. There is no official tinyjs tap or
Homebrew/core submission implied by this file.

## Supported platforms

- macOS 15 (Sequoia) or later, Apple Silicon or Intel.
- Linux is not supported by this formula. The Linux release launcher's system
  dependencies need separate packaging and verification.

Although the macOS launcher targets macOS 14, the bundled txiki.js runtime in
v0.56.0 requires macOS 15. The formula checks the runtime's higher requirement.

## Publish and install a tap

As a tap maintainer, create a GitHub repository named `homebrew-tinyjs` under your
own account or organization. Copy `packaging/homebrew/Formula/tinyjs.rb` into its
`Formula/tinyjs.rb` and commit and push it. Users can then run the following,
replacing `YOUR_ACCOUNT` with that repository's actual owner:

```sh
brew tap YOUR_ACCOUNT/tinyjs
brew install YOUR_ACCOUNT/tinyjs/tinyjs
tinyjs version
tinyjs new hello
cd hello
tinyjs build
```

The formula keeps the release payload under Homebrew's `libexec` directory and
provides a `tinyjs` command on Homebrew's PATH. It needs no separate Node.js
installation for the default template. Framework templates may need their own
package manager, as described in the main README.

If you already have an installation made by the website installer or a source
checkout, check `command -v tinyjs` after installation: an older PATH entry can
take precedence over Homebrew's command. Keep one installation first on PATH.

## Updates

```sh
brew update
brew upgrade tinyjs
```

The Homebrew command wrapper rejects `tinyjs update`, including `--check` and
`--dry-run`: the upstream self-updater replaces its installation directory,
which would bypass Homebrew's version and checksum tracking. The CLI's automatic
update notice may still suggest `tinyjs update`; use `brew upgrade tinyjs` for
this installation. A new upstream release becomes available through Homebrew
only after the tap maintainer updates the formula.

Use `brew uninstall tinyjs` to remove this installation. The wrapper also rejects
`tinyjs uninstall` so it cannot remove files from Homebrew's Cellar directly.

For each release, the tap maintainer must update both architecture URLs (the
version is inferred from them) and both SHA-256 hashes. Download the archives and the release's
`checksums.txt` from the same versioned GitHub release, compute each archive's
SHA-256, and compare it with the published checksum before changing the formula.
Check the runtime's minimum macOS version again if it changes.

Validate changes from the tap repository with its actual qualified formula name:

```sh
ruby -c Formula/tinyjs.rb
brew style YOUR_ACCOUNT/tinyjs/tinyjs
brew audit --strict --online YOUR_ACCOUNT/tinyjs/tinyjs
brew install YOUR_ACCOUNT/tinyjs/tinyjs
brew test --verbose YOUR_ACCOUNT/tinyjs/tinyjs
```

Use `brew reinstall` in place of `brew install` when testing an already installed
version. The formula test checks the installed version, rejection of
self-updates and self-uninstallation, project creation, application build, and the resulting `.app`'s
ad-hoc code signature. It does not open a GUI or verify Intel execution on an
Apple Silicon host.

The `homebrew` GitHub Actions workflow runs these checks in a temporary local tap
on macOS 15 Apple Silicon and Intel runners when the formula or workflow changes.
