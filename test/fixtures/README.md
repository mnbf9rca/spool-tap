# OpenPrintTag sample fixture

`openprinttag_sample.bin` is the 304-byte `docs_src/sample_data/sample_tag.bin` from OpenPrintTag commit `060e420892d66c062c8af440c97701aab96bac79`.
Source: `https://github.com/OpenPrintTag/openprinttag-specification/blob/060e420892d66c062c8af440c97701aab96bac79/docs_src/sample_data/sample_tag.bin`.
SHA-256: `09948415511dcdc41ceca5c33342f50c4822af957cc18ccb6d80808abba7c36c`.
The upstream MIT notice is copied in `LICENSE.openprinttag`.
`openprinttag_sample.inc` contains the same bytes as C initializer text for the device Unity test.

`prusament_openprinttag.bin` is the 245-byte `application/vnd.openprinttag` record payload read from the operator's Prusament ICODE SLIX2 tag on 2026-10-11.
The read-only USB capture is at `/Users/rob/git/spool-tap/.superpowers/sdd/spool-tap/prusa-capture.log`; `prusament_openprinttag.inc` contains the same bytes for the device Unity test.
SHA-256: `6784cc77fff2cfb28297ec73351af103ac65cf547137620e68e69109b5a16153`.
