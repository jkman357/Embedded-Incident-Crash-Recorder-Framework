.PHONY: all validate clean

all: validate

validate:
	./scripts/build_reference.sh

clean:
	rm -rf build validation/build.log validation/reference_release.map \
		validation/reference_development.map validation/reference_off.map \
		validation/reference_hardening.map validation/size_report.txt \
		validation/section_report.txt validation/hardening_disassembly.txt
