TESTS :=

run: $(TESTS)
	@$(foreach i,$(TESTS),./$i && echo $i passed || echo $i failed;)

clean:
	rm $(TESTS)

%: %.c
	gcc $< -o $@
