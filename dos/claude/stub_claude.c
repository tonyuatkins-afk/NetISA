/*
 * stub_claude.c - Fake responses for DOSBox-X testing
 *
 * Keyword-based response selection with simulated ~2-second delay
 * using the BIOS tick counter (INT 1Ah, 18.2 ticks/sec).
 */

#include "claude.h"
#include <string.h>
#include <dos.h>

/* Case-insensitive substring search (no stristr in Watcom) */
static int contains(const char *haystack, const char *needle)
{
    int hlen = (int)strlen(haystack);
    int nlen = (int)strlen(needle);
    int i, j;

    for (i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (j = 0; j < nlen; j++) {
            char h = haystack[i + j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/* Case-insensitive whole-word search: needle must be at start/end
 * of haystack or bounded by non-alpha characters. */
static int contains_word(const char *haystack, const char *needle)
{
    int hlen = (int)strlen(haystack);
    int nlen = (int)strlen(needle);
    int i, j;

    for (i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        char before, after;
        for (j = 0; j < nlen; j++) {
            char h = haystack[i + j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) { match = 0; break; }
        }
        if (!match) continue;
        /* Check word boundaries */
        before = (i > 0) ? haystack[i - 1] : ' ';
        after = (i + nlen < hlen) ? haystack[i + nlen] : ' ';
        if (before >= 'a' && before <= 'z') continue;
        if (before >= 'A' && before <= 'Z') continue;
        if (after >= 'a' && after <= 'z') continue;
        if (after >= 'A' && after <= 'Z') continue;
        return 1;
    }
    return 0;
}

/* Wait ~2 seconds using BIOS tick counter */
static void stub_delay(void)
{
    unsigned long start = 0, now = 0;

    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr start, dx
        mov word ptr start+2, cx
    }

    for (;;) {
        _asm {
            xor ax, ax
            int 1Ah
            mov word ptr now, dx
            mov word ptr now+2, cx
        }
        if ((now - start) >= 36) break;  /* ~2 seconds at 18.2 Hz */
        _asm { int 28h }  /* DOS idle: yield to TSRs */
    }
}

int stub_claude_respond(const char *user_msg, char *response, int max_len)
{
    const char *reply;

    stub_delay();

    /* Check for command output feedback */
    if (contains(user_msg, "command output:")) {
        reply = "I can see the command output. Let me check if there's "
                "anything noteworthy there. In a real session I'd analyze "
                "the output and suggest next steps. Try asking me to run "
                "another command!";
    }
    else if (contains(user_msg, "hello") || contains(user_msg, "hi")) {
        reply = "Hello! I'm Claude, running on a vintage DOS PC through a "
                "NetISA card. The fact that this 8088 is having a "
                "conversation with me via TLS 1.3 is genuinely remarkable. "
                "What would you like to talk about?";
    }
    else if (contains(user_msg, "ibm") || contains_word(user_msg, "pc") ||
             contains(user_msg, "5150")) {
        reply = "The IBM PC 5150, released August 12, 1981, was a landmark "
                "in personal computing. It used an Intel 8088 at 4.77 MHz "
                "with 16KB to 256KB of RAM. IBM's decision to use "
                "off-the-shelf components and publish the technical "
                "reference manual enabled the entire PC clone industry.";
    }
    else if (contains(user_msg, "netisa")) {
        reply = "NetISA is the card making this conversation possible. It's "
                "an ISA expansion card with an ATF1508AS CPLD for bus "
                "interfacing and an ESP32-S3 for WiFi, TLS 1.3, and "
                "application logic. Your DOS PC sends my API request "
                "through the card, which handles all the encryption and "
                "networking.";
    }
    else if (contains(user_msg, "dos")) {
        reply = "MS-DOS is beautifully simple. A single-tasking operating "
                "system with direct hardware access, no memory protection, "
                "and a flat real-mode address space. For a text-mode chat "
                "interface like this one, DOS is actually ideal because you "
                "get instant keyboard response and direct video buffer "
                "writes at 0xB8000.";
    }
    else if (contains(user_msg, "cathode")) {
        reply = "Cathode is the text-mode web browser being built for "
                "NetISA. It renders HTML as CP437 characters with 16-color "
                "ANSI attributes. The HTML parsing happens on the ESP32, so "
                "even an 8088 can browse the modern web.";
    }
    else if (contains(user_msg, "dir") || contains(user_msg, "run") ||
             contains(user_msg, "execute") || contains(user_msg, "command") ||
             contains(user_msg, "test")) {
        reply = "I can run commands on this machine in Agent mode! Here, "
                "let me check what's in the current directory:\n\n"
                "[EXEC]dir[/EXEC]\n\n"
                "Switch to Ask or Auto mode with F4 to enable command "
                "execution.";
    }
    else if (contains(user_msg, "help")) {
        reply = "I can discuss vintage computing, NetISA features, DOS "
                "programming, hardware design, or anything else. Press F4 "
                "to enable Agent mode where I can also run DOS commands on "
                "this machine. Available modes:\n\n"
                "  Chat - conversation only (default)\n"
                "  Ask  - I propose commands, you confirm\n"
                "  Auto - I run commands freely\n\n"
                "Ask me anything!";
    }
    else {
        reply = "That's an interesting question! On a real NetISA card with "
                "API access, I'd give you a thoughtful response. Right now "
                "this is a stub running in DOSBox-X. When the hardware "
                "arrives, this exact interface connects to the real Claude "
                "API over TLS 1.3.";
    }

    strncpy(response, reply, max_len - 1);
    response[max_len - 1] = '\0';
    return (int)strlen(response);
}
