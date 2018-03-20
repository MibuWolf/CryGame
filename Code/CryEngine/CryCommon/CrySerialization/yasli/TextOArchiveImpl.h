/**
 *  yasli - Serialization Library.
 *  Copyright (C) 2007-2013 Evgeny Andreeshchev <eugene.andreeshchev@gmail.com>
 *                          Alexander Kotliar <alexander.kotliar@gmail.com>
 * 
 *  This code is distributed under the MIT License:
 *                          http://www.opensource.org/licenses/MIT
 */

#pragma once

#include "TextOArchive.h"
#include "MemoryWriter.h"
#include <float.h>

namespace yasli{

// Some of non-latin1 characters here are not escaped to
// keep compatibility with 8-bit local encoding (e.g. windows-1251)
static const char* escapeTable[256] = {
    "\\0" /* 0x00: */,
    "\\x01" /* 0x01: */,
    "\\x02" /* 0x02: */,
    "\\x03" /* 0x03: */,
    "\\x04" /* 0x04: */,
    "\\x05" /* 0x05: */,
    "\\x06" /* 0x06: */,
    "\\x07" /* 0x07: */,
    "\\x08" /* 0x08: */,
    "\\t"   /* 0x09: \t */,
    "\\n"   /* 0x0A: \n */,
    "\\x0B" /* 0x0B: */,
    "\\x0C" /* 0x0C: */,
    "\\r" /* 0x0D: */,
    "\\x0E" /* 0x0E: */,
    "\\x0F" /* 0x0F: */,
    
    
    "\\x10" /* 0x10: */,
    "\\x11" /* 0x11: */,
    "\\x12" /* 0x12: */,
    "\\x13" /* 0x13: */,
    "\\x14" /* 0x14: */,
    "\\x15" /* 0x15: */,
    "\\x16" /* 0x16: */,
    "\\x17" /* 0x17: */,
    "\\x18" /* 0x18: */,
    "\\x19" /* 0x19: */,
    "\\x1A" /* 0x1A: */,
    "\\x1B" /* 0x1B: */,
    "\\x1C" /* 0x1C: */,
    "\\x1D" /* 0x1D: */,
    "\\x1E" /* 0x1E: */,
    "\\x1F" /* 0x1F: */,
    
    
    " " /* 0x20:   */,
    "!" /* 0x21: ! */,
    "\\\"" /* 0x22: " */,
    "#" /* 0x23: # */,
    "$" /* 0x24: $ */,
    "%" /* 0x25: % */,
    "&" /* 0x26: & */,
    "'" /* 0x27: ' */,
    "(" /* 0x28: ( */,
    ")" /* 0x29: ) */,
    "*" /* 0x2A: * */,
    "+" /* 0x2B: + */,
    "," /* 0x2C: , */,
    "-" /* 0x2D: - */,
    "." /* 0x2E: . */,
    "/" /* 0x2F: / */,
    
    
    "0" /* 0x30: 0 */,
    "1" /* 0x31: 1 */,
    "2" /* 0x32: 2 */,
    "3" /* 0x33: 3 */,
    "4" /* 0x34: 4 */,
    "5" /* 0x35: 5 */,
    "6" /* 0x36: 6 */,
    "7" /* 0x37: 7 */,
    "8" /* 0x38: 8 */,
    "9" /* 0x39: 9 */,
    ":" /* 0x3A: : */,
    ";" /* 0x3B: ; */,
    "<" /* 0x3C: < */,
    "=" /* 0x3D: = */,
    ">" /* 0x3E: > */,
    "?" /* 0x3F: ? */,
    
    
    "@" /* 0x40: @ */,
    "A" /* 0x41: A */,
    "B" /* 0x42: B */,
    "C" /* 0x43: C */,
    "D" /* 0x44: D */,
    "E" /* 0x45: E */,
    "F" /* 0x46: F */,
    "G" /* 0x47: G */,
    "H" /* 0x48: H */,
    "I" /* 0x49: I */,
    "J" /* 0x4A: J */,
    "K" /* 0x4B: K */,
    "L" /* 0x4C: L */,
    "M" /* 0x4D: M */,
    "N" /* 0x4E: N */,
    "O" /* 0x4F: O */,
    
    
    "P" /* 0x50: P */,
    "Q" /* 0x51: Q */,
    "R" /* 0x52: R */,
    "S" /* 0x53: S */,
    "T" /* 0x54: T */,
    "U" /* 0x55: U */,
    "V" /* 0x56: V */,
    "W" /* 0x57: W */,
    "X" /* 0x58: X */,
    "Y" /* 0x59: Y */,
    "Z" /* 0x5A: Z */,
    "[" /* 0x5B: [ */,
    "\\\\" /* 0x5C: \ */,
    "]" /* 0x5D: ] */,
    "^" /* 0x5E: ^ */,
    "_" /* 0x5F: _ */,
    
    
    "`" /* 0x60: ` */,
    "a" /* 0x61: a */,
    "b" /* 0x62: b */,
    "c" /* 0x63: c */,
    "d" /* 0x64: d */,
    "e" /* 0x65: e */,
    "f" /* 0x66: f */,
    "g" /* 0x67: g */,
    "h" /* 0x68: h */,
    "i" /* 0x69: i */,
    "j" /* 0x6A: j */,
    "k" /* 0x6B: k */,
    "l" /* 0x6C: l */,
    "m" /* 0x6D: m */,
    "n" /* 0x6E: n */,
    "o" /* 0x6F: o */,
    
    
    "p" /* 0x70: p */,
    "q" /* 0x71: q */,
    "r" /* 0x72: r */,
    "s" /* 0x73: s */,
    "t" /* 0x74: t */,
    "u" /* 0x75: u */,
    "v" /* 0x76: v */,
    "w" /* 0x77: w */,
    "x" /* 0x78: x */,
    "y" /* 0x79: y */,
    "z" /* 0x7A: z */,
    "{" /* 0x7B: { */,
    "|" /* 0x7C: | */,
    "}" /* 0x7D: } */,
    "~" /* 0x7E: ~ */,
    "\x7F" /* 0x7F: */, // for utf-8
    
    
    "\x80" /* 0x80: */,
    "\x81" /* 0x81: */,
    "\x82" /* 0x82: */,
    "\x83" /* 0x83: */,
    "\x84" /* 0x84: */,
    "\x85" /* 0x85: */,
    "\x86" /* 0x86: */,
    "\x87" /* 0x87: */,
    "\x88" /* 0x88: */,
    "\x89" /* 0x89: */,
    "\x8A" /* 0x8A: */,
    "\x8B" /* 0x8B: */,
    "\x8C" /* 0x8C: */,
    "\x8D" /* 0x8D: */,
    "\x8E" /* 0x8E: */,
    "\x8F" /* 0x8F: */,
    
    
    "\x90" /* 0x90: */,
    "\x91" /* 0x91: */,
    "\x92" /* 0x92: */,
    "\x93" /* 0x93: */,
    "\x94" /* 0x94: */,
    "\x95" /* 0x95: */,
    "\x96" /* 0x96: */,
    "\x97" /* 0x97: */,
    "\x98" /* 0x98: */,
    "\x99" /* 0x99: */,
    "\x9A" /* 0x9A: */,
    "\x9B" /* 0x9B: */,
    "\x9C" /* 0x9C: */,
    "\x9D" /* 0x9D: */,
    "\x9E" /* 0x9E: */,
    "\x9F" /* 0x9F: */,
    
    
    "\xA0" /* 0xA0: */,
    "\xA1" /* 0xA1: Ў */,
    "\xA2" /* 0xA2: ў */,
    "\xA3" /* 0xA3: Ј */,
    "\xA4" /* 0xA4: ¤ */,
    "\xA5" /* 0xA5: Ґ */,
    "\xA6" /* 0xA6: ¦ */,
    "\xA7" /* 0xA7: § */,
    "\xA8" /* 0xA8: Ё */,
    "\xA9" /* 0xA9: © */,
    "\xAA" /* 0xAA: Є */,
    "\xAB" /* 0xAB: « */,
    "\xAC" /* 0xAC: ¬ */,
    "\xAD" /* 0xAD: ­ */,
    "\xAE" /* 0xAE: ® */,
    "\xAF" /* 0xAF: Ї */,
    
    
    "\xB0" /* 0xB0: ° */,
    "\xB1" /* 0xB1: ± */,
    "\xB2" /* 0xB2: І */,
    "\xB3" /* 0xB3: і */,
    "\xB4" /* 0xB4: ґ */,
    "\xB5" /* 0xB5: µ */,
    "\xB6" /* 0xB6: ¶ */,
    "\xB7" /* 0xB7: · */,
    "\xB8" /* 0xB8: ё */,
    "\xB9" /* 0xB9: № */,
    "\xBA" /* 0xBA: є */,
    "\xBB" /* 0xBB: » */,
    "\xBC" /* 0xBC: ј */,
    "\xBD" /* 0xBD: Ѕ */,
    "\xBE" /* 0xBE: ѕ */,
    "\xBF" /* 0xBF: ї */,
    
    
    "\xC0" /* 0xC0: А */,
    "\xC1" /* 0xC1: Б */,
    "\xC2" /* 0xC2: В */,
    "\xC3" /* 0xC3: Г */,
    "\xC4" /* 0xC4: Д */,
    "\xC5" /* 0xC5: Е */,
    "\xC6" /* 0xC6: Ж */,
    "\xC7" /* 0xC7: З */,
    "\xC8" /* 0xC8: И */,
    "\xC9" /* 0xC9: Й */,
    "\xCA" /* 0xCA: К */,
    "\xCB" /* 0xCB: Л */,
    "\xCC" /* 0xCC: М */,
    "\xCD" /* 0xCD: Н */,
    "\xCE" /* 0xCE: О */,
    "\xCF" /* 0xCF: П */,
    
    
    "\xD0" /* 0xD0: Р */,
    "\xD1" /* 0xD1: С */,
    "\xD2" /* 0xD2: Т */,
    "\xD3" /* 0xD3: У */,
    "\xD4" /* 0xD4: Ф */,
    "\xD5" /* 0xD5: Х */,
    "\xD6" /* 0xD6: Ц */,
    "\xD7" /* 0xD7: Ч */,
    "\xD8" /* 0xD8: Ш */,
    "\xD9" /* 0xD9: Щ */,
    "\xDA" /* 0xDA: Ъ */,
    "\xDB" /* 0xDB: Ы */,
    "\xDC" /* 0xDC: Ь */,
    "\xDD" /* 0xDD: Э */,
    "\xDE" /* 0xDE: Ю */,
    "\xDF" /* 0xDF: Я */,
    
    
    "\xE0" /* 0xE0: а */,
    "\xE1" /* 0xE1: б */,
    "\xE2" /* 0xE2: в */,
    "\xE3" /* 0xE3: г */,
    "\xE4" /* 0xE4: д */,
    "\xE5" /* 0xE5: е */,
    "\xE6" /* 0xE6: ж */,
    "\xE7" /* 0xE7: з */,
    "\xE8" /* 0xE8: и */,
    "\xE9" /* 0xE9: й */,
    "\xEA" /* 0xEA: к */,
    "\xEB" /* 0xEB: л */,
    "\xEC" /* 0xEC: м */,
    "\xED" /* 0xED: н */,
    "\xEE" /* 0xEE: о */,
    "\xEF" /* 0xEF: п */,
    
    
    "\xF0" /* 0xF0: р */,
    "\xF1" /* 0xF1: с */,
    "\xF2" /* 0xF2: т */,
    "\xF3" /* 0xF3: у */,
    "\xF4" /* 0xF4: ф */,
    "\xF5" /* 0xF5: х */,
    "\xF6" /* 0xF6: ц */,
    "\xF7" /* 0xF7: ч */,
    "\xF8" /* 0xF8: ш */,
    "\xF9" /* 0xF9: щ */,
    "\xFA" /* 0xFA: ъ */,
    "\xFB" /* 0xFB: ы */,
    "\xFC" /* 0xFC: ь */,
    "\xFD" /* 0xFD: э */,
    "\xFE" /* 0xFE: ю */,
    "\xFF" /* 0xFF: я */
};

void escapeString(MemoryWriter& dest, const char* begin, const char* end)
{
    while(begin != end){
        const char* str = escapeTable[(unsigned char)(*begin)];
        dest.write(str);
        ++begin;
    }
}

// ---------------------------------------------------------------------------

static const int TAB_WIDTH = 2;

TextOArchive::TextOArchive(int textWidth, const char* header)
: Archive(OUTPUT | TEXT)
, header_(header)
, textWidth_(textWidth)
, compactOffset_(0)
{
    buffer_.reset(new MemoryWriter(1024, true));
    if(header_)
        (*buffer_) << header_;

    YASLI_ASSERT(stack_.empty());
    stack_.push_back(Level(false, 0, 0));
}

TextOArchive::~TextOArchive()
{
}

bool TextOArchive::save(const char* fileName)
{
    YASLI_ESCAPE(fileName && strlen(fileName) > 0, return false);
    YASLI_ESCAPE(stack_.size() == 1, return false);
    YASLI_ESCAPE(buffer_.get() != 0, return false);
    YASLI_ESCAPE(buffer_->position() <= buffer_->size(), return false);
    stack_.pop_back();
    if(FILE* file = fopen(fileName, "wb")){
        if(fwrite(buffer_->c_str(), 1, buffer_->position(), file) != buffer_->position()){
            fclose(file);
            return false;
        }
        fclose(file);
        return true;
    }
    else{
        return false;
    }
}

const char* TextOArchive::c_str() const
{
    return buffer_->c_str();
}

size_t TextOArchive::length() const
{
	return buffer_->position();
}

void TextOArchive::openBracket()
{
	*buffer_ << "{";
}

void TextOArchive::closeBracket()
{
	*buffer_ << "}";
}

void TextOArchive::openContainerBracket()
{
    *buffer_ << "[";
}

void TextOArchive::closeContainerBracket()
{
    *buffer_ << "]";
}

void TextOArchive::placeName(const char* name)
{
    if(name[0] != '\0'){
        *buffer_ << name;
        *buffer_ << " = ";
    }
}

void TextOArchive::placeIndent()
{
	if (buffer_->size())
	*buffer_ << "\r\n";
  int count = int(stack_.size() - 1);
	stack_.back().indentCount += count;
  for(int i = 0; i < count; ++i)
	*buffer_ << "\t";
  compactOffset_ = 0;
}

void TextOArchive::placeIndentCompact()
{
  if ((compactOffset_ % 32) != 0 && stack_.back().isContainer){
		*buffer_ << " ";
		compactOffset_ += 1;
	}
  else if (buffer_->size())
  {
	*buffer_ << "\r\n";
    int count = int(stack_.size() - 1);
    stack_.back().indentCount += count/* * TAB_WIDTH*/;
    for(int i = 0; i < count; ++i)
        *buffer_ << "\t";
	 compactOffset_ = 1;
  }
}

bool TextOArchive::operator()(bool& value, const char* name, const char* label)
{
    placeIndent();
    placeName(name);
    *buffer_ << (value ? "true" : "false");
    return true;
}


bool TextOArchive::operator()(StringInterface& value, const char* name, const char* label)
{
    placeIndent();
    placeName(name);
    (*buffer_) << "\""; 
    const char* str = value.get();
    escapeString(*buffer_, str, str + strlen(value.get()));
    (*buffer_) << "\"";
    return true;
}

inline char* writeUtf16ToUtf8(char* s, unsigned int ch)
{
  const unsigned char byteMark = 0x80;
  const unsigned char byteMask = 0xBF;

  size_t len;

  if (ch < 0x80)
    len = 1;
  else if (ch < 0x800)
    len = 2;
  else if (ch < 0x10000)
    len = 3;
  else if (ch < 0x200000)
    len = 4;
  else
    return s;

  s += len;

  const unsigned char firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
  switch(len)
  {
  case 4:
  *--s = (char)((ch | byteMark) & byteMask); 
  ch >>= 6;
  case 3:
  *--s = (char)((ch | byteMark) & byteMask); 
  ch >>= 6;
  case 2:
  *--s = (char)((ch | byteMark) & byteMask); 
  ch >>= 6;
  case 1:
  *--s = (char)(ch | firstByteMark[len]);
  }

  return s + len;
}

bool TextOArchive::operator()(WStringInterface& value, const char* name, const char* label)
{
	placeIndent();
	placeName(name);
	(*buffer_) << "\""; 

	const wchar_t* in = value.get();
  for(; *in; ++in)
  {
    char buf[6];
		escapeString(*buffer_, buf, writeUtf16ToUtf8(buf, *in));
  }

	(*buffer_) << "\"";
	return true;
}

bool TextOArchive::operator()(float& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(double& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(i32& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(u32& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(i16& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(u16& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(i64& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(u64& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(u8& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(i8& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(char& value, const char* name, const char* label)
{
    placeIndentCompact();
    placeName(name);
    (*buffer_) << value;
    return true;
}

bool TextOArchive::operator()(const Serializer& ser, const char* name, const char* label)
{
    placeIndent();
    placeName(name);
    std::size_t position = buffer_->position();
    openBracket();
    stack_.push_back(Level(false, position, int(strlen(name) + 2 * (name[0] & 1) + (stack_.size() - 1) * TAB_WIDTH + 2)));

    YASLI_ASSERT(ser);
    ser(*this);

    bool joined = joinLinesIfPossible();
    stack_.pop_back();
    if(!joined)
        placeIndent();
    else
        *buffer_ << " ";
    closeBracket();
    return true;
}


bool TextOArchive::operator()(ContainerInterface& ser, const char* name, const char* label)
{
    placeIndent();
    placeName(name);
    std::size_t position = buffer_->position();
    openContainerBracket();
    stack_.push_back(Level(true, position, int(strlen(name) + 2 * (name[0] & 1) + stack_.size() - 1 * TAB_WIDTH + 2)));

    std::size_t size = ser.size();
    if(size > 0){
        do{
            ser(*this, "", "");
        }while(ser.next());
    }

    bool joined = joinLinesIfPossible();
    stack_.pop_back();
    if(!joined)
        placeIndent();
		else
			*buffer_ << " ";
		closeContainerBracket();
    return true;
}

static char* joinLines(char* start, char* end)
{
    YASLI_ASSERT(start <= end);
    char* next = start;
    while(next != end){
        if(*next != '\t' && *next != '\r'){
            if(*next != '\n')
                *start = *next;
            else
                *start = ' ';
            ++start;
        }
        ++next;
    }
    return start;
}

bool TextOArchive::joinLinesIfPossible()
{
    YASLI_ASSERT(!stack_.empty());
    std::size_t startPosition = stack_.back().startPosition;
    YASLI_ASSERT(startPosition < buffer_->size());
    int indentCount = stack_.back().indentCount;
    //YASLI_ASSERT(startPosition >= indentCount);
    if(buffer_->position() - startPosition - indentCount < std::size_t(textWidth_)){
        char* buffer = buffer_->buffer();
        char* start = buffer + startPosition;
        char* end = buffer + buffer_->position();
        end = joinLines(start, end);
        std::size_t newPosition = end - buffer;
        YASLI_ASSERT(newPosition <= buffer_->position());
        buffer_->setPosition(newPosition);
        return true;
    }
    return false;
}

}
// vim:ts=4 sw=4:
