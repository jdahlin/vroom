// auto in variable declarations: [dcl.spec.auto]/4 and /7,
// [dcl.type.auto.deduct]/2 and /4 (deduction as from a call), with
// cv-qualifiers, pointers, references, forwarding references, arrays and
// functions decaying, at namespace and block scope, in init-statements and
// conditions; a trailing return type ([dcl.fct]/2)
int numbers[4] = {1, 2, 3, 4};
int triple(int v) { return v * 3; }

auto global = 40;
const auto global_const = 2.5;
auto global_pointer = &global;
auto decayed = numbers;

auto trailing(int v) -> long { return v * 100L; }
auto trailing_ref(int &v) -> int & { return v; }

struct Pair {
    int first;
    short second;
};

Pair make_pair(int a) { return Pair{a, 2}; }

int main() {
    int failures = 0;
    auto i = 5;
    auto l = 5L;
    auto c = 'c';
    auto d = 1.5;
    failures += sizeof(i) != 4 || sizeof(l) != 8 || sizeof(c) != 1;
    failures += d != 1.5;
    const auto ci = i;
    auto &ri = i;
    const auto &cri = i;
    auto *pi = &i;
    const auto *cpi = &i;
    ri = 6;
    failures += i != 6 || cri != 6 || *pi != 6 || *cpi != 6 || ci != 5;
    auto &&forwarded = i;
    forwarded = 8;
    failures += i != 8;
    auto &&temporary = i + 1;
    temporary += 1;
    failures += temporary != 10;
    auto direct(i), braced{i};
    failures += direct != 8 || braced != 8;
    auto x = 1, *px = &x;
    *px = 3;
    failures += x != 3;
    auto array = numbers;
    failures += array[2] != 3 || sizeof(array) != 8;
    auto &whole = numbers;
    failures += sizeof(whole) != 16 || whole[3] != 4;
    auto function = triple;
    auto &function_ref = triple;
    failures += function(2) != 6 || function_ref(3) != 9;
    auto pair = make_pair(7);
    failures += pair.first != 7 || pair.second != 2;
    auto copy = pair;
    copy.first = 1;
    failures += pair.first != 7;
    const auto unsigned_value = 3u;
    failures += unsigned_value - 4 < 3;
    int sum = 0;
    for (auto k = 0; k < 4; k++)
        sum += numbers[k];
    failures += sum != 10;
    if (auto found = triple(0))
        failures++;
    else
        failures += found != 0;
    switch (auto key = global % 7) {
    case 5:
        break;
    default:
        failures++;
    }
    while (auto left = sum--)
        if (left < 8)
            break;
    failures += *global_pointer != 40 || global_const != 2.5;
    failures += decayed[0] != 1;
    failures += trailing(2) != 200;
    trailing_ref(i) = 12;
    failures += i != 12;
    return failures;
}
