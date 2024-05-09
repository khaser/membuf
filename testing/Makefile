TESTS := ABA_dev.sh

run: $(TESTS)
	@$(foreach i,$(TESTS),./$i && echo $i passed || echo $i failed;)

clean:
	rm $(patsubst %.c,%.o,$(wildcard *.c))

%: %.c
	gcc $< -o $@
