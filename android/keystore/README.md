# Sideload signing key

`zh-mobile-sideload.jks` signs the APKs built by GitHub Actions. It is committed on
purpose: every build must be signed with the **same** key, otherwise Android refuses to
update the installed app and the player would have to uninstall it — which also deletes
the imported game data (~2.5 GB).

This key is only for sideloaded builds (GitHub Releases). If the app is ever published
on Google Play, use Play App Signing with a separate, private upload key instead.
