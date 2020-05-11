# AudioLoopback

A userspace macOS audio loopback driver.

Mostly based on [Kyle Neideck's BackgroundMusic app](https://github.com/kyleneideck/BackgroundMusic)


Build:

```
# make sure your signing cert is configured in the project (a Developer ID, not your personal Apple Developer certificate)

make
sudo rsync -Pav -EH  build/Products/Debug/RDCAudio.driver/ /Library/Audio/Plug-Ins/HAL/RDCAudio.driver/ # a simple cp breaks extended attributes
sudo pkill -9 coreaudiod
```

Notarization is required to install on other Macs. With your Apple ID password stored as `AC_PASSWORD` in the Keychain:

```
make notarize
```

Wait for a confirmation e-mail from Apple and:

```
make staple
```

