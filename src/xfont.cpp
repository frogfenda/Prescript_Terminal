#include <Arduino.h>
#include <xfont.h>
#ifdef ARDUINO_GFX
    #include "Arduino_GFX_Library.h"
#elif defined(TFT_ESPI)
    #include <TFT_eSPI.h>   
#endif

XFont::XFont()
{
    XFont(false);
}

#ifdef ARDUINO_GFX
    XFont::XFont(Arduino_GFX *gfx_tft)
    {
        unsigned long beginTime = millis();
        InitTFT(gfx_tft);
        Serial.printf("     TFT初始化耗时:%2f 秒.\r\n",(millis() - beginTime)/1000.0);
        beginTime = millis();
        initZhiku(fontFilePath);
        Serial.printf("     装载字符集耗时:%2f 秒.\r\n",(millis() - beginTime)/1000.0);
    }
#endif

XFont::XFont(bool isTFT)
{
    unsigned long beginTime = millis();
    if(isTFT==true)
    {
        InitTFT();
        Serial.printf("     TFT初始化耗时:%2f 秒.\r\n",(millis() - beginTime)/1000.0);
    }

    beginTime = millis();
    initZhiku(fontFilePath);
    Serial.printf("     装载字符集耗时:%2f 秒.\r\n",(millis() - beginTime)/1000.0);
}

XFont:: ~XFont(void){
    clear();
}

void XFont:: clear(void){
    if(fontFile)fontFile.close();
    strAllUnicodes=String("");
}

void XFont::InitTFT()
{
#ifdef ARDUINO_GFX
    if (!tft->begin())
    {
        Serial.println("gfx->begin() failed!");
    }
    #ifdef GFX_BL
        pinMode(GFX_BL, OUTPUT);
        digitalWrite(GFX_BL, HIGH);
    #endif
    tft->setRotation(1);
    tft->setCursor(10, 10);
    tft->fillScreen(BLACK);
    tft->setTextColor(GREEN);
#elif defined(TFT_ESPI)
    // 【魔改】：跳过物理 TFT 初始化，交由我们自己的 HAL 层接管
#endif
    isTftInited=true;
}

#ifdef ARDUINO_GFX
    void XFont:: InitTFT(Arduino_GFX *gfx_tft){
        tft=gfx_tft;
        if (!tft->begin())
        {
            Serial.println("gfx->begin() failed!");
        }
        #ifdef GFX_BL
            pinMode(GFX_BL, OUTPUT);
            digitalWrite(GFX_BL, HIGH);
        #endif
        tft->setRotation(1);
        tft->setCursor(10, 10);
        tft->fillScreen(BLACK);
        tft->setTextColor(GREEN);     
        isTftInited=true;   
    }
#endif  

String XFont::getStringFromChars(uint8_t *bs, int l)
{
    String ret;
    char* input2 = (char *) bs;
    ret = input2;
    return ret.substring(0,l);
}

String XFont::getUnicodeFromUTF8(String s)
{
    char character[s.length()];
    String string_to_hex = "";
    int n = 0;
    for (uint16_t i = 0; i < s.length(); i++)
    {
        if (s[i] >= 1 and s[i] <= 127)
        {
            String ss1 = String(s[i], HEX);
            ss1 = ss1.length() == 1 ? "0" + ss1 : ss1;
            string_to_hex += "00" + ss1;
        }
        else
        {
            character[n + 1] = ((s[i + 1] & 0x3) << 6) + (s[i + 2] & 0x3F);
            character[n] = ((s[i] & 0xF) << 4) + ((s[i + 1] >> 2) & 0xF);
            String ss1 = String(character[n], HEX);
            String ss2 = String(character[n + 1], HEX);
            string_to_hex += ss1.length() == 1 ? "0" + ss1 : ss1;
            string_to_hex += ss2.length() == 1 ? "0" + ss2 : ss2;
            n = n + 2;
            i = i + 2;
        }
    }
    return string_to_hex;
}

int XFont::getFontPage(int font_size, int bin_type)
{
    int total = font_size * font_size;
    int hexCount = 8;
    if (bin_type == 32)
        hexCount = 10;
    if (bin_type == 64)
        hexCount = 12;
    int hexAmount = int(total / hexCount);
    if (total % hexCount > 0)
    {
        hexAmount += 1;
    }
    return hexAmount * 2;
}

String XFont::getPixDataFromHex(String s)
{
    int l = s.length();
    int cnt=0;
    String ret="";
    int cc = 5;

    for (int ii = 0; ii < l; ii++)
    {
        int d=strchr( s64,s[ii])-s64;
        for (int k = cc; k >= 0; k--)
        {
            ret+=(String)bitRead(d, k);
            cnt++;
        }
    }
    return ret;
}

void XFont::reInitZhiku(String fontPath){
    isInit=false;
    initZhiku( fontPath);
}

void XFont::initZhiku(String fontPath)
{
    if (isInit == true)
        return;
    if(LittleFS.begin()==false){
        Serial.println("littleFS 文件系统初始化失败,请检查相关配置。");
        return;
    }
    if (LittleFS.exists(fontPath))
    {
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
        do
        {
            size_t k = read_size;
            if (laststr < read_size)
                k = laststr;
            fontFile.read(buf_total_str_unicode2, k);
            strAllUnicodes += getStringFromChars(buf_total_str_unicode2, k);
            laststr -= read_size;
        } while (laststr > 0);
        free(buf_total_str_unicode2);
#endif

        Serial.printf("     字库中字符总数:%d \r\n",strAllUnicodes.length()/5);
        unicode_begin_idx = 6 + 2 + 2 + total_font_cnt * 5;
        isInit = true;
        fontFile.close();
    }
    else
    {
        Serial.println("LitteFS系统工作正常，但是找不到字库文件。");
    }
}

String XFont::getPixBinStrFromString(String strUnicode)
{
    initZhiku(fontFilePath);
    String ret = "";
    if  (!fontFile)
    {
        fontFile = LittleFS.open(fontFilePath, "r");
        uint8_t buf_seek_pixdata[font_page];
        String ff = "";
        const char* chrAllUnicodes=strAllUnicodes.c_str();
        for (uint16_t i = 0; i < strUnicode.length(); i = i + 4)
        {
            String _str = "u" + strUnicode.substring(i, i + 4);
            
            int p=0; 
            int uIdx = 0;
            #ifndef SAVE_MEMORY 
            char * chrFind=strstr(chrAllUnicodes,_str.c_str());
            p =chrFind-chrAllUnicodes;
            #else
            fontFile.seek(10);
            uint8_t *buf_total_str_unicode2;
            int laststr = font_unicode_cnt;
            int read_size=512*5;
            buf_total_str_unicode2 = (uint8_t *)malloc(read_size);
            int loopcnt=0;
            do
            {
                size_t k = read_size;
                if (laststr < read_size)
                    k = laststr;
                fontFile.read(buf_total_str_unicode2, k);
                String t1=(char*)buf_total_str_unicode2;
                p =t1.indexOf(_str);
                laststr -= read_size;
                if(p>0)break;
                loopcnt++;
            } while (laststr > 0);
            free(buf_total_str_unicode2);

            p=p+loopcnt*512*5;
            #endif
            uIdx = p / 5;
            int pixbeginidx = unicode_begin_idx + uIdx * font_page;
            fontFile.seek(pixbeginidx);
            fontFile.read(buf_seek_pixdata, font_page);
            String su = getStringFromChars(buf_seek_pixdata, font_page);
            String ts=getPixDataFromHex(su);
            ret +=ts;
        }
        fontFile.close();
    }
    return ret;
}

String XFont::getCodeDataFromFile(String strUnicode)
{
    String ret = "";
    if (!fontFile)
    {
        fontFile = LittleFS.open(fontFilePath, "r");
        uint8_t buf_seek_pixdata[font_page];
        String ff = "";
        const char* chrAllUnicodes=strAllUnicodes.c_str();
        for (uint16_t i = 0; i < strUnicode.length(); i = i + 4)
        {
            String _str = "u" + strUnicode.substring(i, i + 4);
            int p=0; 
            int uIdx = 0;
            #ifndef SAVE_MEMORY 
            char * chrFind=strstr(chrAllUnicodes,_str.c_str());
            p =chrFind-chrAllUnicodes;
            #else
            fontFile.seek(10);
            uint8_t *buf_total_str_unicode2;
            int laststr = font_unicode_cnt;
            int read_size=512*5;
            buf_total_str_unicode2 = (uint8_t *)malloc(read_size);
            int loopcnt=0;
            do
            {
                size_t k = read_size;
                if (laststr < read_size)
                    k = laststr;
                fontFile.read(buf_total_str_unicode2, k);
                String t1=(char*)buf_total_str_unicode2;
                p =t1.indexOf(_str);
                laststr -= read_size;
                if(p>0)break;
                loopcnt++;
            } while (laststr > 0);
            free(buf_total_str_unicode2);

            p=p+loopcnt*512*5;
            #endif
            uIdx = p / 5;
            int pixbeginidx = unicode_begin_idx + uIdx * font_page;
            fontFile.seek(pixbeginidx);
            fontFile.read(buf_seek_pixdata, font_page);
            String su = getStringFromChars(buf_seek_pixdata, font_page);
            ret += su;
        }
       fontFile.close();
    }
    return ret;
}

bool XFont::chkAnsi(unsigned char c)
{
    if (c >= 0 && c <= 127)
        return true;
    return false;
}

void XFont::DrawSingleStr(int x, int y, String strBinData, int fontColor,int backColor, bool ansiChar)
{
    unsigned long beginTime = millis();
#ifdef ARDUINO_GFX
    tft->startWrite();
#endif
    for (uint16_t i = 0; i < strBinData.length(); i++)
    {
        int pX1 = int(i % font_size)+x;
        int pY1 = int(i / font_size)+y;
        if(ansiChar){
            uint16_t brk=i%font_size;
            if(brk>(font_size/2))continue;
        }        
        if (strBinData[i] == '1')
        {
            #ifdef ARDUINO_GFX
            tft->writePixelPreclipped(pX1, pY1, fontColor);
            #elif defined(TFT_ESPI)
            // 【魔改】：绑定虚拟画布
            if (tft_sprite != nullptr) tft_sprite->fillRect(pX1, pY1, 1, 1, fontColor);
            #endif
        }
        else{
            #ifdef ARDUINO_GFX
            tft->writePixelPreclipped(pX1, pY1, backColor);
            #elif defined(TFT_ESPI)
            // 【魔改】：绑定虚拟画布
            if (tft_sprite != nullptr) tft_sprite->fillRect(pX1, pY1, 1, 1, backColor);
            #endif                
        }
    }

#ifdef ARDUINO_GFX
    tft->endWrite();
#endif
    time_spent+=millis()  - beginTime;
}

void XFont::DrawSingleStr(int x, int y, String strBinData, int fontColor, bool ansiChar)
{
    unsigned long beginTime = millis();
#ifdef ARDUINO_GFX
    tft->startWrite();
#endif
    for (uint16_t i = 0; i < strBinData.length(); i++)
    {
        if (strBinData[i] == '1')
        {
            int pX1 = int(i % font_size)+x;
            int pY1 = int(i / font_size)+y;
            if(ansiChar){
                uint16_t brk=i%font_size;
                if(brk>(font_size/2))continue;
            }          
#ifdef ARDUINO_GFX
            tft->writePixelPreclipped(pX1, pY1, fontColor);
#elif defined(TFT_ESPI)
            // 【魔改】：绑定虚拟画布
            if (tft_sprite != nullptr) tft_sprite->fillRect(pX1, pY1, 1, 1, fontColor);
#endif
        }
    }
#ifdef ARDUINO_GFX
    tft->endWrite();
#endif
    time_spent+=millis()  - beginTime;
}

String XFont::GetPixDatasFromLib(String displayStr){
    initZhiku(fontFilePath);
    if (isInit == false)
    {
        Serial.println("字库初始化失败");
        return "";
    }
    String ret="";

    String strUnicode = getUnicodeFromUTF8(displayStr);
    for (uint16_t l = 0; l < strUnicode.length() / 4; l++)
    {
        String childUnicode = strUnicode.substring(4 * l, (4) + 4 * l);
        ret += getPixBinStrFromString(childUnicode);
    }
    return ret;
}

void XFont::DrawStr(int x, int y, String str, int fontColor,int backColor)
{
    initZhiku(fontFilePath);
    if (isInit == false)
    {
        Serial.println("字库初始化失败");
        return;
    }
    unsigned long beginTime = millis();

    String strUnicode = getUnicodeFromUTF8(str);
    beginTime = millis();

    singleStrPixsAmount = font_size * font_size;

    int px = x;
    int py = y;
   
    for (uint16_t l = 0; l < strUnicode.length() / 4; l++)
    {
        String childUnicode = strUnicode.substring(4 * l, (4) + 4 * l);
        String childPixData = getPixBinStrFromString(childUnicode);
        u_int sep=1;
        int f = 0;
        sscanf(childUnicode.c_str(), "%x", &f);

        if (f <= 127) 
        {
           if (f == 13 || f == 10) 
            {
                px = 0;
                py += font_size + sep;
                continue;
            }
            else if (f == 9) 
            {
                px += font_size + sep;
                continue;
            }

            if ((px + font_size / 2) > screenWidth)
            {
                px = 0;
                py += font_size + sep;
            }
            if(backColor==-1)DrawSingleStr(px, py, childPixData, fontColor, true);
            else{
                DrawSingleStr(px, py, childPixData, fontColor,backColor, true);
            }

            px += font_size / 2 + sep;
        }
        else
        {
            if ((px + font_size) > screenWidth)
            {
                px = 0;
                py += font_size + sep;
            }
            if(backColor==-1)DrawSingleStr(px, py, childPixData, fontColor, false);
            else{
                DrawSingleStr(px, py, childPixData, fontColor,backColor, false);
            }
            px += font_size + sep;
        }
    }
}

void XFont::DrawStr(int x, int y, String str, int fontColor){
    DrawStr(x,y,str,fontColor,-1);
}

void XFont::DrawChinese(int x, int y, String str, int fontColor)
{
  DrawChinese(x,y,str,fontColor,-1);
}

void XFont::DrawChinese(int x, int y, String str, int fontColor,int backColor){
  if(isTftInited==false){
        Serial.printf(" 调用本方法必须先初始化TFT驱动。.\r\n");
    }
    else{
        DrawStr(x,y,str,fontColor,backColor);
    }
}

void XFont::DrawChineseEx(int x, int y, String str, int fontColor,int backColor)
{
    if(isTftInited==false){
        Serial.printf(" 调用本方法必须先初始化TFT驱动。.\r\n");
    }
    else{
         DrawStrEx(x,y,str,fontColor,backColor);
    }
}

void XFont::DrawChineseEx(int x, int y, String str, int fontColor){
    DrawChineseEx(x,y,str,fontColor,-1);
}

void XFont::DrawStrEx(int x, int y, String str, int fontColor,int backColor)
{
    initZhiku(fontFilePath);
    if (isInit == false)
    {
        Serial.println("字库初始化失败");
        return;
    }

    String strUnicode = getUnicodeFromUTF8(str);
    String codeData=getCodeDataFromFile(strUnicode);
    singleStrPixsAmount = font_size * font_size;

    int px = x;
    int py = y;

    for (uint16_t l = 0; l < strUnicode.length() / 4; l++)
    {
        String childUnicode = strUnicode.substring(4 * l, 4 * (l+1));
        String childCodeData = codeData.substring(font_page * l, font_page * (l+1));
        String childPixData = getPixDataFromHex(childCodeData);
        int f = 0;
        sscanf(childUnicode.c_str(), "%x", &f);
        u_int sep=1;

        if (f <= 127) 
        {
            if (f == 13 || f == 10) 
            {
                px = 0;
                py += font_size + sep;
                continue;
            }
            else if (f == 9) 
            {
                px += font_size + sep;
                continue;
            }

            if(backColor==-1)DrawSingleStr(px, py, childPixData, fontColor, true);
            else{
                DrawSingleStr(px, py, childPixData, fontColor,backColor, true);
            }

            px += font_size / 2 + sep;
        }
        else
        {
            if ((px + font_size) > screenWidth)
            {
                px = 0;
                py += font_size + sep;
            }
            if(backColor==-1)DrawSingleStr(px, py, childPixData, fontColor, false);
            else{
                DrawSingleStr(px, py, childPixData, fontColor,backColor, false);
            }
            px += font_size + sep;
        }
    }
}

void XFont::DrawStrEx(int x, int y, String str, int fontColor){
    DrawStrEx(x,y,str,fontColor,-1);
}