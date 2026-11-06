# Include alongside the production Makefile after building the core. Compile
# the reference algorithm with the same current headers, configuration and ABI.
.PHONY: spv-export-reference
spv-export-reference:
	@test -n "$(REFERENCE_SOURCE)" -a -n "$(REFERENCE_OBJECT)"
	@mkdir -p "$(dir $(REFERENCE_OBJECT))"
	$(CXX) $(BASE_FLAGS) $(CPPFLAGS) $(CXXFLAGS) -c "$(REFERENCE_SOURCE)" -o "$(REFERENCE_OBJECT)"
