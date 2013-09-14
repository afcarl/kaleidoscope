LLVM_INCL=`llvm-config --cppflags`
LLVM_LINK=`llvm-config --ldflags --libs core`
FLAGS=-Wno-c++11-extensions -g
CXX=clang++

TARGET=kc
OBJECTS=lexer.o ast.o parser.o codegen.o

.phony: clean all
all: $(TARGET)

%.o: %.cc
	$(CXX) $(LLVM_INCL) $(FLAGS) -c $<

$(TARGET): $(OBJECTS)
	$(CXX) $(LLVM_LINK) $(FLAGS) $^ -o $@

clean:
	rm -rf *.o *.dSYM $(TARGET)
