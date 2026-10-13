#include "qr_printer.h"
#include <stdio.h>
#include <stdbool.h>
#include "qrcode.h"

void qr_printer_console(const char *text) {
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, ECC_MEDIUM, text);

    printf("\n=======================================================\n");
    printf("   [스마트폰 카메라로 스캔하여 웹서버 즉시 접속]       \n");
    printf("   URL: %s\n", text);
    printf("=======================================================\n\n");

    for (int y = 0; y < qrcode.size; y += 2) {
        printf("    "); // 좌측 여백
        for (int x = 0; x < qrcode.size; x++) {
            bool upper = qrcode_getModule(&qrcode, x, y);
            bool lower = (y + 1 < qrcode.size) ? qrcode_getModule(&qrcode, x, y + 1) : false;

            if (upper && lower) {
                printf("\xE2\x96\x88"); // █
            } else if (upper && !lower) {
                printf("\xE2\x96\x80"); // ▀
            } else if (!upper && lower) {
                printf("\xE2\x96\x84"); // ▄
            } else {
                printf(" ");
            }
        }
        printf("\n");
    }
    printf("\n=======================================================\n\n");
}
