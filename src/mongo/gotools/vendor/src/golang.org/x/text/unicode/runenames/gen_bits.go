// Copyright 2016 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// +build ignore

package main

// This file contains code common to gen.go and the package code.

// The mapping from rune to string (i.e. offset and length in the data string)
// is encoded as a two level table. The first level maps from contiguous rune
// ranges [runeOffset, runeOffset+runeLength) to entries. Entries are either
// direct (for repeated names such as "<CJK Ideograph>") or indirect (for runs
// of unique names such as "SPACE", "EXCLAMATION MARK", "QUOTATION MARK", ...).
//
// Each first level table element is 64 bits. The runeOffset (21 bits) and
// runeLength (16 bits) take the 37 high bits. The entry takes the 27 low bits,
// with directness encoded in the least significant bit.
//
// A direct entry encodes a dataOffset (18 bits) and dataLength (8 bits) in the
// data string. 18 bits is too short to encode the entire data string's length,
// but the data string's contents are arranged so that all of the few direct
// entries' offsets come before all of the many indirect entries' offsets.
//
// An indirect entry encodes a dataBase (10 bits) and a table1Offset (16 bits).
// The table1Offset is the start of a range in the second level table. The
// length of that range is the same as the runeLength.
//
// Each second level table element is 16 bits, an index into data, relative to
// a bias equal to (dataBase << dataBaseUnit). That (bias + index) is the
// (dataOffset + dataLength) in the data string. The dataOffset is implied by
// the previous table element (with the same implicit bias).

const (
	bitsRuneOffset = 21
	bitsRuneLength = 16
	bitsDataOffset = 18
	bitsDataLength = 8
	bitsDirect     = 1

	bitsDataBase     = 10
	bitsTable1Offset = 16

	shiftRuneOffset = 0 + bitsDirect + bitsDataLength + bitsDataOffset + bitsRuneLength
	shiftRuneLength = 0 + bitsDirect + bitsDataLength + bitsDataOffset
	shiftDataOffset = 0 + bitsDirect + bitsDataLength
	shiftDataLength = 0 + bitsDirect
	shiftDirect     = 0

	shiftDataBase     = 0 + bitsDirect + bitsTable1Offset
	shiftTable1Offset = 0 + bitsDirect

	maskRuneLength = 1<<bitsRuneLength - 1
	maskDataOffset = 1<<bitsDataOffset - 1
	maskDataLength = 1<<bitsDataLength - 1
	maskDirect     = 1<<bitsDirect - 1

	maskDataBase     = 1<<bitsDataBase - 1
	maskTable1Offset = 1<<bitsTable1Offset - 1

	dataBaseUnit = 10
)
