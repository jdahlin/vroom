# 29.6.2 Stream buffer requirements [streambuf.reqts]

[Contents](../../README.md) · up: [29.6 Stream buffers \[stream.buffers\]](stream.buffers.md) · prev: [29.6.1 Header `<streambuf>` synopsis \[streambuf.syn\]](streambuf.syn.md) · next: [29.6.3 Class template `basic_streambuf` \[streambuf\]](streambuf.md)

**1** Stream buffers can impose various constraints on the sequences they control. Some constraints are:

- The controlled input sequence can be not readable.
- The controlled output sequence can be not writable.
- The controlled sequences can be associated with the contents of other representations for character sequences, such as external files.
- The controlled sequences can support operations *directly* to or from associated sequences.
- The controlled sequences can impose limitations on how the program can read characters from a sequence, write characters to a sequence, put characters back into an input sequence, or alter the stream position.

**2** Each sequence is characterized by three pointers which, if non-null, all point into the same `charT` array object. The array object represents, at any moment, a (sub)sequence of characters from the sequence. Operations performed on a sequence alter the values stored in these pointers, perform reads and writes directly to or from associated sequences, and alter “the stream position” and conversion state as needed to maintain this subsequence relationship. The three pointers are:

- the *beginning pointer*, or lowest element address in the array (called `xbeg` here);
- the *next pointer*, or next element address that is a current candidate for reading or writing (called `xnext` here);
- the *end pointer*, or first element address beyond the end of the array (called `xend` here).

**3** The following semantic constraints shall always apply for any set of three pointers for a sequence, using the pointer names given immediately above:

- If `xnext` is not a null pointer, then `xbeg` and `xend` shall also be non-null pointers into the same `charT` array, as described above; otherwise, `xbeg` and `xend` shall also be null.
- If `xnext` is not a null pointer and `xnext < xend` for an output sequence, then a *write position* is available. In this case, `*xnext` shall be assignable as the next element to write (to put, or to store a character value, into the sequence).
- If `xnext` is not a null pointer and `xbeg < xnext` for an input sequence, then a *putback position* is available. In this case, `xnext[-1]` shall have a defined value and is the next (preceding) element to store a character that is put back into the input sequence.
- If `xnext` is not a null pointer and `xnext < xend` for an input sequence, then a *read position* is available. In this case, `*xnext` shall have a defined value and is the next element to read (to get, or to obtain a character value, from the sequence).
