# Native Binary Conversion - Verification Checklist

Use this checklist to verify the conversion was successful.

## File Verification

### Modified Files
- [ ] `build.sh` - Check it runs native SDK compilation (not Docker build)
- [ ] `app/manifest.json` - Check no `runOptions`, has structured `resources.memory.limit`
- [ ] `../POC_README.md` - Check has OS 12 compatibility notes

### New Files Created
- [ ] `Dockerfile.old` - Backup exists
- [ ] `BUILDING_NATIVE.md` - Build documentation exists
- [ ] `../NATIVE_BUILD_MIGRATION.md` - Migration guide exists
- [ ] `../NATIVE_CONVERSION_COMPLETE.md` - Completion report exists
- [ ] `../../NATIVE_CONVERSION_SUMMARY.md` - Summary in root exists

### Source Files Unchanged
- [ ] `app/main.c` - No changes
- [ ] `app/vdo_handler.c` - No changes
- [ ] `app/larod_handler.c` - No changes
- [ ] `app/metadata.c` - No changes
- [ ] `app/dlpu_basic.c` - No changes
- [ ] `app/Makefile` - No changes
- [ ] All other .c/.h files - No changes

## Build Verification

- [ ] Run `./build.sh` completes without errors
- [ ] Package `*.eap` is created
- [ ] Package size is ~5-10MB (not ~100MB)
- [ ] No Docker container metadata in package

## Functional Verification (Optional - requires camera)

- [ ] Deploy to AXIS OS 12 camera
- [ ] Application installs
- [ ] Application starts
- [ ] VDO stream works
- [ ] Larod inference works
- [ ] MQTT publishing works
- [ ] Memory usage ~250MB

## Documentation Verification

- [ ] Read `BUILDING_NATIVE.md` - makes sense
- [ ] Read `../NATIVE_BUILD_MIGRATION.md` - explains changes
- [ ] Read `../NATIVE_CONVERSION_COMPLETE.md` - comprehensive
- [ ] Read `../../NATIVE_CONVERSION_SUMMARY.md` - good overview

## Conversion Status

All items checked? Conversion is complete and verified!

---
**Last Updated:** November 23, 2025
