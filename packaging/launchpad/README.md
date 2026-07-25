# Launchpad Packaging

This directory keeps DynLex's Ubuntu/Launchpad packaging isolated from the
project root.

## What lives here

- `debian/`: Debian metadata template used to build Launchpad source uploads
- `scripts/`: helper scripts for building and uploading source packages
- `source-excludes.txt`: paths excluded from the upstream source tarball

## Local release flow

1. Install the packaging toolchain:

   ```bash
   ./packaging/launchpad/scripts/install-build-deps.sh
   ```

2. Build a Launchpad source package for an Ubuntu series:

   ```bash
   ./packaging/launchpad/scripts/build-source-package.sh \
     --series noble \
     --version 0.1.0 \
     --ppa-revision 1
   ```

3. Upload it after importing a Launchpad-registered signing key:

   ```bash
   ./packaging/launchpad/scripts/build-source-package.sh \
     --series noble \
     --version 0.1.0 \
     --ppa-revision 1 \
     --gpg-key YOURKEYID \
     --upload-target ppa:YOUR_LAUNCHPAD_ID/dynlex
   ```

## GitHub Actions

The `launchpad-ppa.yml` workflow can:

- build signed source packages for `noble`, `plucky`, and `questing`
- upload them to a PPA with `dput`
- publish the generated `.changes`, `.dsc`, and tarballs as workflow artifacts

The source package embeds a clean snapshot of the exact sparse LLVM checkout
recorded in `metadata/LLVM_TOOLCHAIN`; Git internals are excluded and provenance
is recorded beside the snapshot. Launchpad builds that source locally, so
package builds neither depend on an Ubuntu LLVM version nor fetch code from the
network.

Repository configuration:

- Variable `LAUNCHPAD_PPA`: `owner/ppa-name`
- Secret `LAUNCHPAD_GPG_PRIVATE_KEY`: ASCII-armored private key
- Secret `LAUNCHPAD_GPG_PASSPHRASE`: passphrase for the private key
- Secret `LAUNCHPAD_GPG_KEY_ID`: key ID used for signing

Launchpad only accepts signed Debian source uploads. The GitHub workflow
builds the source package on GitHub and Launchpad builds the final binaries.
