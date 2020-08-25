all:
	xcodebuild -scheme RDCAudio X_DEVICE_NAME="$(DEVICE_NAME)" X_DEVICE_MANUFACTURER_NAME="$(DEVICE_MANUFACTURER_NAME)" X_BUNDLE_ID="$(BUNDLE_ID)"

notarize:
	ditto -c -k --keepParent Build/Products/Debug/RDCAudio.driver RDCAudio.driver.notarize.zip

	xcrun altool --notarize-app --primary-bundle-id "driver" --username "$(APPLE_ID)" --password "@keychain:AC_PASSWORD" --file RDCAudio.driver.notarize.zip
	rm *.notarize.zip

staple:
	xcrun stapler staple Build/Products/Debug/RDCAudio.driver

.PHONY: notarize staple
