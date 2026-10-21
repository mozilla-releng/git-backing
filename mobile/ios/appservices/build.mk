package:
	$(call py_action,package_appservices_ios,$(ABS_DIST)/appservices-ios)

upload:
	$(PYTHON3) -u $(MOZILLA_DIR)/build/upload.py $(ABS_DIST)/appservices-ios/*.zip
