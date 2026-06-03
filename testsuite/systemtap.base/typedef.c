struct my_s { int a; };
typedef struct my_s my_t;
my_t x;

int main() {
    x.a = 42;
    return 0;
}
