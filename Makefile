all:
	xcodebuild -scheme RDCAudio

notarize:
	ditto -c -k --keepParent Build/Products/Debug/RDCAudio.driver RDCAudio.driver.notarize.zip

	xcrun altool --notarize-app --primary-bundle-id "ec.driver" --username "e.pankov@elements.tv" --password "@keychain:AC_PASSWORD" --file RDCAudio.driver.notarize.zip
	rm *.notarize.zip

staple:
	xcrun stapler staple Build/Products/Debug/RDCAudio.driver

.PHONY: notarize staple
