// [dcl.attr.fallthrough]/1: `[[fallthrough]];` at the end of a compound
// statement inside a `case` before the next `case` or `default` label
// (e.g. OpenTTD's `newgrf_station.h:86`).

int classify(int x)
{
    int r = 0;
    switch (x) {
    case 1: {
        r += 10;
        [[fallthrough]];
    }
    default:
        r += 5;
        break;
    }
    return r;
}

int main()
{
    if (classify(1) != 15 || classify(2) != 5)
        return 1;
    return 0;
}
