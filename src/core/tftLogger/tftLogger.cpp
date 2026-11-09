    r = BRUCE_TFT_DRIVER::drawString(string, x, y, font);
    restoreLogger();
    return r;
}

int16_t tft_logger::drawCentreString(const String &string, int32_t x, int32_t y, uint8_t font) {
    log_drawString(string, DRAWCENTRESTRING, x, y);
    int16_t r;
    if (isSleeping) return string.length();
    r = BRUCE_TFT_DRIVER::drawCentreString(string, x, y, font);
    restoreLogger();
    return r;
}

int16_t tft_logger::drawRightString(const String &string, int32_t x, int32_t y, uint8_t font) {
    log_drawString(string, DRAWRIGHTSTRING, x, y);
    int16_t r;
    if (isSleeping) return string.length();
    r = BRUCE_TFT_DRIVER::drawRightString(string, x, y, font);
    restoreLogger();
    return r;
}

void tft_logger::log_print(String s) {
    if (!logging) return;
    if (!log) return;

    removeLogEntriesInsideRect(
        getCursorX() - 1, getCursorY() - 1, s.length() * LW * currentTextSize() + 2,
        LH * currentTextSize() + 2
    );

    uint8_t buffer[MAX_LOG_SIZE];
    uint8_t pos = 0;
    logWriteHeader(buffer, pos, PRINT);

    writeUint16(buffer, pos, getCursorX());
    writeUint16(buffer, pos, getCursorY());
    writeUint16(buffer, pos, currentTextSize());
    writeUint16(buffer, pos, currentTextColor());
    writeUint16(buffer, pos, currentTextBgColor());

    size_t maxLen = MAX_LOG_SIZE - pos - 1;
    size_t len = s.length();
    if (len > maxLen) len = maxLen;
    memcpy(buffer + pos, s.c_str(), len);
    pos += len;

    buffer[1] = pos;

    tftLog l;
    memcpy(l.data, buffer, pos);
    pushLogIfUnique(l);
}

size_t tft_logger::print(const String &s) {
    size_t totalPrinted = 0;
    int remaining = s.length();
    int offset = 0;

    const int maxChunkSize = MAX_LOG_SIZE - 13; // 13 bytes reserved to header + metadata

    while (remaining > 0) {
        int chunkSize = (remaining > maxChunkSize) ? maxChunkSize : remaining;
        String chunk = s.substring(offset, offset + chunkSize);

        log_print(chunk);
        if (isSleeping) totalPrinted += chunk.length();
        else totalPrinted += BRUCE_TFT_DRIVER::print(chunk);

        offset += chunkSize;
        remaining -= chunkSize;
    }

    return totalPrinted;
}

size_t tft_logger::println(void) { return print("\n"); }

size_t tft_logger::println(const String &s) { return print(s + "\n"); }

size_t tft_logger::println(char c) { return print(String(c) + "\n"); }

size_t tft_logger::println(unsigned char b, int base) { return print(String(b, base) + "\n"); }

size_t tft_logger::println(int n, int base) { return print(String(n, base) + "\n"); }

size_t tft_logger::println(unsigned int n, int base) { return print(String(n, base) + "\n"); }

size_t tft_logger::println(long n, int base) { return print(String(n, base) + "\n"); }

size_t tft_logger::println(unsigned long n, int base) { return print(String(n, base) + "\n"); }

size_t tft_logger::println(long long n, int base) { return print(String(n, base) + "\n"); }

size_t tft_logger::println(unsigned long long n, int base) { return print(String(n, base) + "\n"); }

size_t tft_logger::println(double n, int digits) { return print(String(n, digits) + "\n"); }

size_t tft_logger::print(char c) { return print(String(c)); }

size_t tft_logger::print(unsigned char b, int base) { return print(String(b, base)); }

size_t tft_logger::print(int n, int base) { return print(String(n, base)); }

size_t tft_logger::print(unsigned int n, int base) { return print(String(n, base)); }

size_t tft_logger::print(long n, int base) { return print(String(n, base)); }

size_t tft_logger::print(unsigned long n, int base) { return print(String(n, base)); }

size_t tft_logger::print(long long n, int base) { return print(String(n, base)); }

size_t tft_logger::print(unsigned long long n, int base) { return print(String(n, base)); }

size_t tft_logger::print(double n, int digits) { return print(String(n, digits)); }

size_t tft_logger::printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buf[256];
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return print(String(buf));
}
