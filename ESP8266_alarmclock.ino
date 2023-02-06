/* forked from: https://github.com/flyAkari/ESP8266_Network_Clock
 * 本代码适用于ESP8266 NodeMCU + 12864显示屏
 * 7pin SPI引脚，正面看，从左到右依次为GND、VCC、D0、D1、RES、DC、CS
 *    ESP8266 ---  OLED
 *      3V    ---  VCC
 *      G     ---  GND
 *      D7    ---  D1
 *      D5    ---  D0
 *      D2orD8---  CS
 *      D1    ---  DC
 *      RST   ---  RES
 * 4pin IIC引脚，正面看，从左到右依次为GND、VCC、SCL、SDA
 *      ESP8266  ---  OLED
 *      3.3V     ---  VCC
 *      G (GND)  ---  GND
 *      D1(GPIO5)---  SCL
 *      D2(GPIO4)---  SDA
 * 		d8		 ---  闹钟
 * 		d7		 ---  主按键
 * 		d6		 ---  调节按键
 */
char m = 1;													// 0为24小时，1为上午，2为下午；若需开启12小时制，则填1或2均可
static const char ntpServerName[] = "ntp1.aliyun.com";		// NTP服务器，阿里云
static const char backupNtpServerName[] = "ntp.ntsc.ac.cn"; // 备用NTP服务器
const int timeZone = 8;										// 时区，北京时间为+8
const char *ssid = "HUAWEI-1B9Q19ybx";
const char *password = "13530479550";
const bool enableAlarmDays[] = {1, 1, 1, 1, 1, 1, 1}; // 哪天要开启闹钟，分别对应周日到周六
static int scrnoff = 23;							  // 屏幕晚上关闭的时间，24小时制，不想关填24
static int scrnon = 8;								  // 屏幕早上开启时间，24小时制，不想关填0
static float alarmTime = 12.13;						  // 闹钟时间，24小时制！！！关闭填25
#define outputPin 15								  // 继电器脚
#define mainPin 13									  // 确认/关闭/小睡？按钮
#define adjPin 12									  // 调整脚

#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
// #include <DYWiFiConfig.h>
#include <U8g2lib.h>

// 若屏幕使用SH1106，只需把SSD1306改为SH1106即可
// U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/4, /* dc=*/5, /* reset=*/3);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 4, /* data=*/ 5); //D-duino
int LEDstatus;
WiFiUDP Udp;
unsigned int localPort = 8888; // 用于侦听UDP数据包的本地端口
static int alarmTimeHour;	   // 闹钟时间小时
static int alarmTimeMinute;	   // 闹钟时间分钟
bool alarmDisengaged = false;

time_t getNtpTime();
void sendNTPpacket(IPAddress &address);
void oledClockDisplay();
void sendCommand(int command, int value);
void initdisplay();

boolean isNTPConnected = false;

const unsigned char xing[] U8X8_PROGMEM = {
	0x00, 0x00, 0xF8, 0x0F, 0x08, 0x08, 0xF8, 0x0F, 0x08, 0x08, 0xF8, 0x0F, 0x80, 0x00, 0x88, 0x00,
	0xF8, 0x1F, 0x84, 0x00, 0x82, 0x00, 0xF8, 0x0F, 0x80, 0x00, 0x80, 0x00, 0xFE, 0x3F, 0x00, 0x00}; /*星*/
const unsigned char liu[] U8X8_PROGMEM = {
	0x40, 0x00, 0x80, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x00,
	0x20, 0x02, 0x20, 0x04, 0x10, 0x08, 0x10, 0x10, 0x08, 0x10, 0x04, 0x20, 0x02, 0x20, 0x00, 0x00}; /*六*/

void setup()
{
	Serial.begin(115200);
	pinMode(outputPin, OUTPUT);
	while (!Serial)
		continue;
	initdisplay();
	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_unifont_t_chinese2);
	u8g2.setCursor(0, 14);
	u8g2.print("Connecting to");
	u8g2.setCursor(0, 30);
	u8g2.print(ssid);
	u8g2.sendBuffer();
	Serial.println("OLED Ready");

	alarmTimeHour = (int)alarmTime;
	alarmTimeMinute = (alarmTime - (int)alarmTime) * 100;
	Serial.print("alarm time: ");
	Serial.print(alarmTimeHour);
	Serial.println(alarmTimeMinute);

	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	while (!Serial)
		continue;
	Serial.print("Connecting to ");
	Serial.println(ssid);
	pinMode(2, OUTPUT);
	while (WiFi.status() != WL_CONNECTED)
	{
		LEDstatus = 1 - LEDstatus;
		digitalWrite(2, LEDstatus);
		delay(500);
		Serial.print(".");
	}
	Serial.println("");
	Serial.println("WiFi connected");
	LEDstatus = 1;
	digitalWrite(2, LEDstatus);
	Serial.println(WiFi.localIP());

	Serial.println("Starting UDP");
	Udp.begin(localPort);
	Serial.print("Local port: ");
	Serial.println(Udp.localPort());
	Serial.println("waiting for sync");
	setSyncProvider(getNtpTime);
	setSyncInterval(300); // 每300秒同步一次时间
	isNTPConnected = true;
}

time_t prevDisplay = 0; // 当时钟已经显示

void loop()
{
	if (timeStatus() != timeNotSet)
	{
		if (now() != prevDisplay)
		{ // 时间改变时更新显示
			prevDisplay = now();
			oledClockDisplay();

			// Serial.println(checkIfEnableToday());
			// Serial.println("test point 1");
			digitalWrite(outputPin, checkIfNeedAlarmNow());
		}
	}
	// wificonfig.handle(); // 若不需要Web后台，可以注释掉此行
	if (!digitalRead(mainPin))
		alarmDisengaged = true;
	if (!digitalRead(adjPin))
		settingsPage();
}
void settingsPage()
{
	static String settingsItem[] = {"adjust alarm time", "adjust alarm date"};
	int cursor = -1;
	for (;; ESP.wdtFeed())
	{
		if (!digitalRead(adjPin))
		{
			cursor++;

			u8g2.clearBuffer();
			if (cursor > 2)
				cursor = 0;
			u8g2.setCursor(0, 14);
			if (cursor == 0)
				u8g2.print(">");
			else
				u8g2.print(" ");
			u8g2.print(settingsItem[0]);

			u8g2.setCursor(0, 30);
			if (cursor == 1)
				u8g2.print(">");
			else
				u8g2.print(" ");
			u8g2.print(settingsItem[1]);

			u8g2.setCursor(0, 64);
			if (cursor == 2)
				u8g2.print(">");
			else
				u8g2.print(" ");
			u8g2.print("return");

			u8g2.sendBuffer();
			delay(250);
		}
		if (!digitalRead(adjPin))
		{
		}

		if (!digitalRead(mainPin))
		{
			if (cursor == 2)
				break;
		}
	}
}
bool checkIfEnableToday()
{
	// Serial.println("test point 2");
	return enableAlarmDays[weekday() - 1];
}
bool checkIfOnAlarmTime()
{
	if (hour() == alarmTimeHour && minute() == alarmTimeMinute)
		return true;
	else
		return false;
}
bool checkIfNeedAlarmNow()
{
	if (second() == 0)
		alarmDisengaged = false;
	if (checkIfEnableToday() && checkIfOnAlarmTime())
	{
		if (alarmDisengaged)
			return false;
		return true;
	}
	else
		return false;
}

void initdisplay()
{
	u8g2.begin();
	u8g2.enableUTF8Print();
}

void oledClockDisplay()
{
	int years, months, days, hours, minutes, seconds, weekdays;
	years = year();
	months = month();
	days = day();
	hours = hour();
	minutes = minute();
	seconds = second();
	weekdays = weekday();
	Serial.printf("%d/%d/%d %d:%d:%d Weekday:%d\n", years, months, days, hours, minutes, seconds, weekdays);
	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_unifont_t_chinese2);
	u8g2.setCursor(0, 14);
	if (isNTPConnected)
		u8g2.print("当前时间 (UTC+8)");
	else
		u8g2.print("无网络!"); // 如果上次对时失败，则会显示无网络

	String currentTime = "";
	if (m != 0)
	{
		if (hours < 12)
			m = 1;
		else
			m = 2;
	}
	if (m == 2)
		hours -= 12;
	if (hours == 0)
		hours = 12;
	if (hours < 10)
		currentTime += 0;
	currentTime += hours;
	currentTime += ":";
	if (minutes < 10)
		currentTime += 0;
	currentTime += minutes;
	currentTime += ":";
	if (seconds < 10)
		currentTime += 0;
	currentTime += seconds;
	if (m != 0)
	{
		if (m == 2)
			currentTime += "P";
		else
			currentTime += "A";
	}

	String currentDay = "";
	currentDay += years;
	currentDay += "/";
	if (months < 10)
		currentDay += 0;
	currentDay += months;
	currentDay += "/";
	if (days < 10)
		currentDay += 0;
	currentDay += days;
	u8g2.setFont(u8g2_font_logisoso24_tr);
	u8g2.setCursor(0, 44);
	u8g2.print(currentTime);
	u8g2.setCursor(0, 61);
	u8g2.setFont(u8g2_font_unifont_t_chinese2);
	u8g2.print(currentDay);
	u8g2.drawXBM(80, 48, 16, 16, xing);
	u8g2.setCursor(95, 62);
	u8g2.print("期");
	if (weekdays == 1)
		u8g2.print("日");
	else if (weekdays == 2)
		u8g2.print("一");
	else if (weekdays == 3)
		u8g2.print("二");
	else if (weekdays == 4)
		u8g2.print("三");
	else if (weekdays == 5)
		u8g2.print("四");
	else if (weekdays == 6)
		u8g2.print("五");
	else if (weekdays == 7)
		u8g2.drawXBM(111, 49, 16, 16, liu);
	u8g2.sendBuffer();
}

#pragma region /*-------- NTP 代码 ----------*/

const int NTP_PACKET_SIZE = 48;		// NTP时间在消息的前48个字节里
byte packetBuffer[NTP_PACKET_SIZE]; // 输入输出包的缓冲区

time_t getNtpTime()
{
	IPAddress ntpServerIP; // NTP服务器的地址

	while (Udp.parsePacket() > 0)
		; // 丢弃以前接收的任何数据包
	Serial.println("Transmit NTP Request");
	// 从池中获取随机服务器
	WiFi.hostByName(ntpServerName, ntpServerIP);
getNtpTime:
	Serial.print(ntpServerName);
	Serial.print(": ");
	Serial.println(ntpServerIP);
	sendNTPpacket(ntpServerIP);
	uint32_t beginWait = millis();
	while (millis() - beginWait < 3000)
	{
		int size = Udp.parsePacket();
		if (size >= NTP_PACKET_SIZE)
		{
			Serial.println("Receive NTP Response");
			isNTPConnected = true;
			Udp.read(packetBuffer, NTP_PACKET_SIZE); // 将数据包读取到缓冲区
			unsigned long secsSince1900;
			// 将从位置40开始的四个字节转换为长整型，只取前32位整数部分
			secsSince1900 = (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];
			Serial.println(secsSince1900);
			Serial.println(secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
			return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
		}
	}
	Serial.println("No NTP Response"); // 无NTP响应
	isNTPConnected = false;
	if (ntpServerName != backupNtpServerName)
	{
		WiFi.hostByName(backupNtpServerName, ntpServerIP);
		goto getNtpTime;
	}
	return 0; // 如果未得到时间则返回0
}

// 向给定地址的时间服务器发送NTP请求
void sendNTPpacket(IPAddress &address)
{
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	packetBuffer[0] = 0b11100011; // LI, Version, Mode
	packetBuffer[1] = 0;		  // Stratum, or type of clock
	packetBuffer[2] = 6;		  // Polling Interval
	packetBuffer[3] = 0xEC;		  // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	Udp.beginPacket(address, 123); // NTP需要使用的UDP端口号为123
	Udp.write(packetBuffer, NTP_PACKET_SIZE);
	Udp.endPacket();
}
#pragma endregion