#include <Arduino.h>
#include <xfont.h>
#ifdef ARDUINO_GFX
    #include "Arduino_GFX_Library.h"
#elif defined(TFT_ESPI)
    #include <TFT_eSPI.h>   
#endif

XFont::XFont() { XFont(false); }

XFont::XFont(bool isTFT) {
    if(isTFT==true) InitTFT();
    initZhiku(fontFilePath);
}

XFont:: ~XFont(void){ clear(); }

void XFont:: clear(void){
    if(fontFile)fontFile.close();
    strAllUnicodes=String("");
}

void XFont::InitTFT() {
    isTftInited=true;
}

// 【终极兼容防弹版】：逐字节安全追加，绝不越界，绝不报错
String XFont::getStringFromChars(uint8_t *bs, int l) {
    String ret;
    ret.reserve(l + 1); // 提前分配好内存，防止碎片化
    for(int i = 0; i < l; i++) {
        ret += (char)bs[i];
    }
    return ret;
}

String XFont::getUnicodeFromUTF8(String s) {
    char character[s.length()];
    String string_to_hex = "";
    int n = 0;
    for (uint16_t i = 0; i < s.length(); i++) {
        if (s[i] >= 1 and s[i] <= 127) {
            String ss1 = String(s[i], HEX);
            ss1 = ss1.length() == 1 ? "0" + ss1 : ss1;
            string_to_hex += "00" + ss1;
        } else {
            character[n + 1] = ((s[i + 1] & 0x3) << 6) + (s[i + 2] & 0x3F);
            character[n] = ((s[i] & 0xF) << 4) + ((s[i + 1] >> 2) & 0xF);
            String ss1 = String(character[n], HEX);
            String ss2 = String(character[n + 1], HEX);
            string_to_hex += ss1.length() == 1 ? "0" + ss1 : ss1;
            string_to_hex += ss2.length() == 1 ? "0" + ss2 : ss2;
            n = n + 2; i = i + 2;
        }
    }
    return string_to_hex;
}

int XFont::getFontPage(int font_size, int bin_type) {
    int total = font_size * font_size;
    int hexCount = 8;
    if (bin_type == 32) hexCount = 10;
    if (bin_type == 64) hexCount = 12;
    int hexAmount = int(total / hexCount);
    if (total % hexCount > 0) hexAmount += 1;
    return hexAmount * 2;
}

String XFont::getPixDataFromHex(String s) {
    int l = s.length();
    String ret="";
    int cc = 5;
    for (int ii = 0; ii < l; ii++) {
        int d=strchr(s64, s[ii]) - s64;
        for (int k = cc; k >= 0; k--) ret += (String)bitRead(d, k);
    }
    return ret;
}

void XFont::reInitZhiku(String fontPath){
    isInit=false;
    initZhiku(fontPath);
}

void XFont::initZhiku(String fontPath) {
    if (isInit == true) return;
    if(LittleFS.begin()==false) return;
    if (LittleFS.exists(fontPath)) {
        fontFile = LittleFS.open(fontPath, "r");
        static uint8_t buf_total_str[6];
        static uint8_t buf_font_size[2];
        static uint8_t buf_bin_type[2];
        fontFile.read(buf_total_str, 6);
        fontFile.read(buf_font_size, 2);
        fontFile.read(buf_bin_type, 2);

        String s1 = getStringFromChars(buf_total_str, 6);
        String s2 = getStringFromChars(buf_font_size, 2);
        String s3 = getStringFromChars(buf_bin_type, 2);
        total_font_cnt = strtoll(s1.c_str(), NULL, 16);
        font_size = s2.toInt();
        bin_type = s3.toInt();
        font_page = getFontPage(font_size, bin_type);
        font_unicode_cnt = total_font_cnt * 5;

#ifndef SAVE_MEMORY
        strAllUnicodes="";
        uint8_t *buf_total_str_unicode2;
        int laststr = font_unicode_cnt;
        int read_size=512*2;
        buf_total_str_unicode2 = (uint8_t *)malloc(read_size);
        do {
            size_t k = read_size;
            if (laststr < read_size) k = laststr;
            fontFile.read(buf_total_str_unicode2, k);
            strAllUnicodes += getStringFromChars(buf_total_str_unicode2, k);
            laststr -= read_size;
        } while (laststr > 0);
        free(buf_total_str_unicode2);
#endif
        unicode_begin_idx = 6 + 2 + 2 + total_font_cnt * 5;
        isInit = true;
        fontFile.close();
    }
}

String XFont::getCodeDataFromFile(String strUnicode) {
    String ret = "";
    if (!fontFile) {
        fontFile = LittleFS.open(fontFilePath, "r");
        if (!fontFile) return ret; 
        
        uint8_t buf_seek_pixdata[font_page];
        
        for (uint16_t i = 0; i < strUnicode.length(); i = i + 4) {
            String _str = "u" + strUnicode.substring(i, i + 4);
            int uIdx = -1;
            
            #ifndef SAVE_MEMORY 
            const char* chrAllUnicodes=strAllUnicodes.c_str();
            char * chrFind=strstr(chrAllUnicodes,_str.c_str());
            if (chrFind != nullptr) uIdx = (chrFind - chrAllUnicodes) / 5;
            #else
            fontFile.seek(10);
            const int RECORDS_PER_CHUNK = 100; 
            char buf[RECORDS_PER_CHUNK * 5 + 1]; 
            
            for (int chunk_idx = 0; chunk_idx < total_font_cnt; chunk_idx += RECORDS_PER_CHUNK) {
                int to_read = RECORDS_PER_CHUNK;
                if (total_font_cnt - chunk_idx < RECORDS_PER_CHUNK) to_read = total_font_cnt - chunk_idx;
                fontFile.read((uint8_t*)buf, to_read * 5);
                buf[to_read * 5] = '\0'; 
                
                String t1 = buf;
                int p = t1.indexOf(_str);
                if (p != -1 && p % 5 == 0) {
                    uIdx = chunk_idx + (p / 5);
                    break;
                }
            }
            #endif
            
            if (uIdx != -1) {
                int pixbeginidx = unicode_begin_idx + uIdx * font_page;
                fontFile.seek(pixbeginidx);
                fontFile.read(buf_seek_pixdata, font_page);
                ret += getStringFromChars(buf_seek_pixdata, font_page);
            } else {
                for(int b=0; b<font_page; b++) ret += "0";
            }
        }
        fontFile.close();
    }
    return ret;
}

String XFont::getPixBinStrFromString(String strUnicode) {
    String ret = "";
    String codeData = getCodeDataFromFile(strUnicode);
    for (uint16_t l = 0; l < codeData.length() / font_page; l++) {
        String childCodeData = codeData.substring(font_page * l, font_page * (l+1));
        ret += getPixDataFromHex(childCodeData);
    }
    return ret;
}

bool XFont::chkAnsi(unsigned char c) { return (c >= 0 && c <= 127); }

void XFont::DrawSingleStr(int x, int y, String strBinData, int fontColor,int backColor, bool ansiChar) {
    for (uint16_t i = 0; i < strBinData.length(); i++) {
        int pX1 = int(i % font_size)+x;
        int pY1 = int(i / font_size)+y;
        if(ansiChar){ if((i%font_size) > (font_size/2)) continue; }        
        if (strBinData[i] == '1') {
            #ifdef TFT_ESPI
            if (tft_sprite != nullptr) tft_sprite->fillRect(pX1, pY1, 1, 1, fontColor);
            #endif
        } else {
            #ifdef TFT_ESPI
            if (tft_sprite != nullptr) tft_sprite->fillRect(pX1, pY1, 1, 1, backColor);
            #endif                
        }
    }
}

void XFont::DrawSingleStr(int x, int y, String strBinData, int fontColor, bool ansiChar) {
    for (uint16_t i = 0; i < strBinData.length(); i++) {
        if (strBinData[i] == '1') {
            int pX1 = int(i % font_size)+x;
            int pY1 = int(i / font_size)+y;
            if(ansiChar){ if((i%font_size) > (font_size/2)) continue; }          
            #ifdef TFT_ESPI
            if (tft_sprite != nullptr) tft_sprite->fillRect(pX1, pY1, 1, 1, fontColor);
            #endif
        }
    }
}

String XFont::GetPixDatasFromLib(String displayStr){ return ""; }
void XFont::DrawStr(int x, int y, String str, int fontColor,int backColor) {}
void XFont::DrawStr(int x, int y, String str, int fontColor){}
void XFont::DrawChinese(int x, int y, String str, int fontColor) {}
void XFont::DrawChinese(int x, int y, String str, int fontColor,int backColor){}
void XFont::DrawChineseEx(int x, int y, String str, int fontColor,int backColor) { DrawStrEx(x,y,str,fontColor,backColor); }
void XFont::DrawChineseEx(int x, int y, String str, int fontColor){ DrawStrEx(x,y,str,fontColor,-1); }

void XFont::DrawStrEx(int x, int y, String str, int fontColor,int backColor) {
    initZhiku(fontFilePath);
    if (isInit == false) return;
    String strUnicode = getUnicodeFromUTF8(str);
    String codeData=getCodeDataFromFile(strUnicode);
    int px = x; int py = y;
    for (uint16_t l = 0; l < strUnicode.length() / 4; l++) {
        String childUnicode = strUnicode.substring(4 * l, 4 * (l+1));
        String childCodeData = codeData.substring(font_page * l, font_page * (l+1));
        String childPixData = getPixDataFromHex(childCodeData);
        int f = 0; sscanf(childUnicode.c_str(), "%x", &f);
        int sep=1;
        if (f <= 127) {
            if (f == 13 || f == 10) { px = x; py += font_size + sep; continue; }
            else if (f == 9) { px += font_size + sep; continue; }
            if(backColor==-1) DrawSingleStr(px, py, childPixData, fontColor, true);
            else DrawSingleStr(px, py, childPixData, fontColor,backColor, true);
            px += font_size / 2 + sep;
        } else {
            if ((px + font_size) > screenWidth) { px = x; py += font_size + sep; }
            if(backColor==-1) DrawSingleStr(px, py, childPixData, fontColor, false);
            else DrawSingleStr(px, py, childPixData, fontColor,backColor, false);
            px += font_size + sep;
        }
    }
}
void XFont::DrawStrEx(int x, int y, String str, int fontColor){ DrawStrEx(x,y,str,fontColor,-1); }