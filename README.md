# AudioLoopback

A userspace macOS audio loopback driver.

It's derived from [Kyle Neideck's BackgroundMusic app](https://github.com/kyleneideck/BackgroundMusic)


Build:

```
# make sure your signing cert is configured in the project (a Developer ID, not your personal Apple Developer certificate)

BUNDLE_ID=com.acme.AudioDriver DEVICE_NAME="Audio Loopback" DEVICE_MANUFACTURER_NAME="ACME Inc" make
```

Installation:

```
sudo rsync -Pav -EH  build/Products/Debug/RDCAudio.driver/ /Library/Audio/Plug-Ins/HAL/RDCAudio.driver/ # a simple cp breaks extended attributes
sudo pkill -9 coreaudiod
```

Notarization is required to install on other Macs. With your Apple ID password stored as `AC_PASSWORD` in the Keychain:

```
APPLE_ID=john.doe@gmail.com make notarize
```

Wait for a confirmation e-mail from Apple and:

```
make staple
```

