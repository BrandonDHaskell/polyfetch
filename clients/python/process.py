"""CPU-bound stage: streaming SHA-256 plus byte and newline counting."""

import hashlib


class StreamProcessor:
    """Consumes payload bytes as they arrive, computing the SHA-256, the total
    byte count, and the newline (0x0A) count without ever buffering the whole
    payload. Mirrors the Go client's io.MultiWriter(hasher, lineCounter), the
    C++ StreamProcessor, and the TypeScript StreamProcessor.

    Note on the GIL: hashlib's update() is native (OpenSSL) and RELEASES the GIL
    while hashing data of 2KB or more. So the SHA-256 work here is not, by
    itself, GIL-bound — in a thread-based variant it could parallelize across
    cores. It is the *pure-Python* work (the per-chunk loop, attribute access)
    that the GIL serializes. The canonical client sidesteps the question
    entirely by using separate processes (separate GILs); see main.py.
    """

    def __init__(self) -> None:
        self._hash = hashlib.sha256()
        self._size = 0
        self._lines = 0

    def consume(self, chunk: bytes) -> None:
        self._hash.update(chunk)
        self._size += len(chunk)
        # bytes.count is a C-level scan; correct and fast.
        self._lines += chunk.count(b"\n")

    def sha_hex(self) -> str:
        # hexdigest() returns lowercase hex, matching the contract.
        return self._hash.hexdigest()

    def size(self) -> int:
        return self._size

    def lines(self) -> int:
        return self._lines