#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <time.h>
#include "secrets.h"

//----------------------------------------
// TFT Screen Pins (E32R28T / E32N28T 2.8" ESP32-32E ILI9341 board)
//----------------------------------------
#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   -1
#define TFT_BL    21
#define TFT_SCLK  14
#define TFT_MOSI  13
#define TFT_MISO  12

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

const char* tzInfo     = "EST5EDT,M3.2.0,M11.1.0";
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";

//----------------------------------------
// Layout and Gameplay Constants
//----------------------------------------
#define SCREEN_WIDTH         240
#define SCREEN_HEIGHT        320
#define ROWS                 6
#define COLS                 10

#define INVADER_WIDTH        10
#define INVADER_HEIGHT       10
#define INVADER_X_SPACING    10
#define INVADER_Y_SPACING    10
#define INVADER_LEFT_MARGIN  20
#define INVADER_TOP_MARGIN   110

#define BASE_WIDTH           20
#define BASE_HEIGHT          10

#define MISSILE_WIDTH        2
#define MISSILE_HEIGHT       10

#define UFO_WIDTH            16
#define UFO_HEIGHT           7

const unsigned long frameSwitchInterval     = 500;
const unsigned long invaderShotInterval     = 8000;
const unsigned long nonDestructiveInterval  = 15000;
const unsigned long wifiConnectTimeout      = 15000;
const unsigned long ntpSyncTimeout          = 10000;
const unsigned long syncRetryInterval       = 30000;
const unsigned long flashFrameInterval      = 60;
const unsigned long explosionFrameInterval  = 50;
const unsigned long endOfWaveResetDelay     = 1500;
const int invaderMissMinX                   = 6;
const int invaderMissMaxX                   = SCREEN_WIDTH - 7;
const int invaderMissSafetyMargin           = 12;
const int invaderMissRedirectZone           = 32;
const int cannonDodgeSpeed                  = 6;

int  ufoX      = -1;
int  ufoY      = 64;
bool ufoActive = false;

bool          invaderFrame    = false;
unsigned long lastFrameSwitch = 0;

bool invaders[ROWS][COLS];
int  lastMinute = -1;
int  displayedScoreHour = 0;
int  displayedScoreMinute = 0;
bool scoreInitialized = false;
int  pendingScoreUpdates = 0;

struct tm currentTimeInfo = {};
bool      clockHasValidTime = false;
unsigned long lastTimeRecoveryAttempt = 0;
bool pendingDestructiveShot = false;
bool endOfWavePending = false;
unsigned long endOfWaveResetAt = 0;

// Cannon
int  cannonX         = SCREEN_WIDTH / 2 - BASE_WIDTH / 2;
int  cannonDirection = 1;
int  targetX         = -1;
bool topMinuteFire   = false;
bool cannonAligning  = false;
bool cannonDodging   = false;
int  dodgeTargetX    = -1;

// Player missile
int missileX = -1;
int missileY = -1;
bool missileImpactPending = false;

// Invader missile
int invMissileX       = -1;
int invMissileY       = -1;
int invMissileTargetX = -1;
unsigned long lastInvaderShot = 0;
unsigned long lastNonDestructiveAttempt = 0;

// Non-blocking flash/explosion effects
bool flashActive = false;
uint8_t flashFrame = 0;
unsigned long lastFlashTick = 0;

bool explosionActive = false;
int explosionX = -1;
int explosionY = -1;
uint8_t explosionFrame = 0;
unsigned long lastExplosionTick = 0;

uint16_t invaderColors[ROWS] = {
  ILI9341_RED, 0xFC60, ILI9341_YELLOW,
  ILI9341_GREEN, ILI9341_CYAN, ILI9341_PURPLE
};

//----------------------------------------
// Forward Declarations
//----------------------------------------
bool connectWiFiWithTimeout(unsigned long timeoutMs);
bool waitForTimeSync(struct tm &timeInfo, unsigned long timeoutMs);
bool refreshCurrentTime();
void attemptTimeRecovery();
void resetTransientState();
void syncDisplayedScoreToCurrentTime();
void advanceDisplayedScore();

void startScreenFlash();
void updateScreenFlash();

void startExplosion(int x, int y);
void updateExplosion();
void drawExplosionFrameAt(int x, int y, uint16_t color);
void clearExplosionAt(int x, int y);

void drawPixelSafe(int x, int y, uint16_t color);
uint16_t getBackgroundColor(int px, int py);
void drawSpriteFromArray(int x, int y, uint16_t color, const uint8_t sprite[10][10]);
void drawScaledSprite(int x, int y, int w, int h, uint16_t color, const uint8_t sprite[10][10]);
void drawMissile(int x, int y);
void eraseMissile(int x, int y);
void drawInvaderMissileAt(int x, int y, uint16_t color);
void eraseInvaderMissile(int x, int y);

bool destroyOneInvaderAtMissile();
bool findInvaderAtMissile(int &hitRow, int &hitCol);
bool findLastInvaderToDestroy(int &invXCenter, int &invY);
bool anyInvadersAlive();
void fireMissileIfNeeded();
void fireMissile();
int getGapCenter(int gapIndex);
int chooseInvaderMissTargetX();
bool invaderMissileThreatensCannon(int missileBaseX, int missileY);
int getInvaderMissileDrawX(int missileBaseX, int missileY);
void startCannonDodge(int missileBaseX, int missileY);
void setNonDestructiveTarget();
void setDestructiveTarget();
void handleCannonMovement();
void drawScore();
void drawCannon();
void drawInvaders();
void drawScreen();
void initializeInvaders(int minutesElapsed);
void updateMissile();
void updateInvaderMissile();
void triggerUFO();
void updateUFO();
void drawUFOAt(int x, int y, uint16_t color);

//----------------------------------------
// Sprites
//----------------------------------------
const uint8_t cannonSprite[10][10] = {
  {0,0,0,0,1,1,0,0,0,0},
  {0,0,0,0,1,1,0,0,0,0},
  {0,0,0,1,1,1,1,0,0,0},
  {0,0,0,1,1,1,1,0,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1}
};

const uint8_t ufoSprite[7][16] = {
  {0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0},
  {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
  {0,0,1,1,0,1,0,1,1,0,1,0,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
  {1,1,0,1,1,0,1,1,1,1,0,1,1,0,1,1},
  {0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0},
  {0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0}
};

const uint8_t invaderSpriteRow0[10][10] = {
  {1,0,0,0,0,0,0,0,0,1},
  {0,1,1,0,0,0,0,0,1,1},
  {0,0,1,0,1,1,0,1,0,0},
  {0,0,0,1,1,1,1,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,1,1,1,1,1,1,0,1},
  {1,0,0,1,1,1,1,0,0,1},
  {0,1,1,0,0,0,0,1,1,0},
  {0,0,1,0,0,0,0,1,0,0}
};

const uint8_t invaderSpriteRow1[10][10] = {
  {0,0,0,0,1,1,0,0,0,0},
  {0,0,0,1,1,1,1,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,0,1,1,0,1,1,0},
  {1,1,1,0,1,1,0,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,0,1,0,0,1,0,0,0},
  {1,0,1,0,0,0,0,1,0,1},
  {0,1,0,1,1,1,1,0,1,0},
  {1,0,1,0,0,0,0,1,0,1}
};

const uint8_t invaderSpriteRow2[10][10] = {
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,0,1,1,1,1,0,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,0,0,1,1,0,0},
  {0,1,1,0,0,0,0,1,1,0},
  {1,0,0,0,1,1,0,0,0,1},
  {1,0,1,0,0,0,0,1,0,1}
};

const uint8_t invaderSpriteRow3[10][10] = {
  {0,1,0,0,1,1,0,0,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,1,1,1,1,1,1,0,1},
  {1,0,0,1,1,1,1,0,0,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
  {0,1,1,0,0,0,0,1,1,0},
  {1,1,0,0,0,0,0,0,1,1},
  {0,0,1,1,1,1,1,1,1,0}
};

const uint8_t invaderSpriteRow4[10][10] = {
  {0,0,0,1,1,1,1,0,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,0,1,1,0,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,1,1,1,1,1,1,0,1},
  {1,0,0,1,1,1,1,0,0,1},
  {0,1,1,0,1,1,0,1,1,0},
  {0,0,1,0,1,1,0,1,0,0},
  {0,1,1,0,0,0,0,1,1,0},
  {1,0,1,0,0,0,0,1,0,1}
};

const uint8_t invaderSpriteRow5[10][10] = {
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,0,1,1,1,1,1,0,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,1,0,1,1,0,1,0,1},
  {0,1,0,1,1,1,1,0,1,0},
  {1,0,1,0,0,0,0,1,0,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,0,1,1,0,1,1,0}
};

const uint8_t invaderSpriteRow0B[10][10] = {
  {0,0,1,0,0,0,0,0,1,0},
  {0,0,0,1,0,0,0,1,0,0},
  {0,0,1,0,1,1,0,1,0,0},
  {0,0,0,1,1,1,1,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,1,1,1,1,1,1,0,1},
  {1,0,0,1,1,1,1,0,0,1},
  {1,1,0,0,0,0,0,0,1,1},
  {0,0,1,1,0,0,1,1,0,0}
};

const uint8_t invaderSpriteRow1B[10][10] = {
  {0,0,0,0,1,1,0,0,0,0},
  {0,0,0,1,1,1,1,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,0,1,1,0,1,1,0},
  {1,1,1,0,1,1,0,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,0,1,0,0,1,0,0,1},
  {0,1,0,0,1,1,0,0,1,0},
  {0,0,1,0,0,0,0,1,0,0},
  {0,1,0,0,0,0,0,0,1,0}
};

const uint8_t invaderSpriteRow2B[10][10] = {
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,0,1,1,1,1,0,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,0,0,0,0,1,0,0},
  {0,1,0,0,1,1,0,0,1,0},
  {1,0,0,0,0,0,0,0,0,1}
};

const uint8_t invaderSpriteRow3B[10][10] = {
  {0,1,0,0,1,1,0,0,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,1,1,1,1,1,1,0,1},
  {1,0,0,1,1,1,1,0,0,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,0,0,0,0,1,1,0},
  {1,0,0,1,0,0,1,0,0,1},
  {0,1,1,0,0,0,0,1,1,0},
  {1,0,0,1,1,1,1,0,0,1}
};

const uint8_t invaderSpriteRow4B[10][10] = {
  {0,0,0,1,1,1,1,0,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,0,1,1,0,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,1,1,1,1,1,1,0,1},
  {1,0,0,1,1,1,1,0,0,1},
  {1,1,0,0,1,1,0,0,1,1},
  {0,0,1,0,0,0,0,1,0,0},
  {0,0,0,1,0,0,1,0,0,0},
  {0,0,1,0,0,0,0,1,0,0}
};

const uint8_t invaderSpriteRow5B[10][10] = {
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,0,1,1,1,1,1,0,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,0,1,1,1,1,0,1,0},
  {1,0,1,0,1,1,0,1,0,1},
  {0,1,0,1,0,0,1,0,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,1,1,0,1,1,0,1,1,0},
  {1,0,0,1,0,0,1,0,0,1}
};

const uint8_t explosionSprite[7][13] = {
  {0,0,1,0,0,0,1,0,0,0,1,0,0},
  {0,1,0,1,0,0,0,0,0,1,0,1,0},
  {1,0,0,0,1,0,0,0,1,0,0,0,1},
  {0,0,0,1,0,1,0,1,0,1,0,0,0},
  {1,0,0,0,1,0,0,0,1,0,0,0,1},
  {0,1,0,1,0,0,0,0,0,1,0,1,0},
  {0,0,1,0,0,0,1,0,0,0,1,0,0}
};

const uint8_t (*const invaderSpritesA[ROWS])[10] = {
  invaderSpriteRow0,
  invaderSpriteRow1,
  invaderSpriteRow2,
  invaderSpriteRow3,
  invaderSpriteRow4,
  invaderSpriteRow5
};

const uint8_t (*const invaderSpritesB[ROWS])[10] = {
  invaderSpriteRow0B,
  invaderSpriteRow1B,
  invaderSpriteRow2B,
  invaderSpriteRow3B,
  invaderSpriteRow4B,
  invaderSpriteRow5B
};

const uint16_t explosionColors[3] = {
  ILI9341_WHITE,
  ILI9341_YELLOW,
  0xFB60
};

//----------------------------------------
// Connectivity / Time
//----------------------------------------
bool connectWiFiWithTimeout(unsigned long timeoutMs) {
  if (ssid[0] == '\0') {
    Serial.println("WiFi credentials missing; starting unsynced.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    return true;
  }

  Serial.println("WiFi timeout; starting unsynced.");
  return false;
}

bool waitForTimeSync(struct tm &timeInfo, unsigned long timeoutMs) {
  Serial.print("Waiting for NTP");
  unsigned long start = millis();
  while ((millis() - start) < timeoutMs) {
    if (getLocalTime(&timeInfo, 100)) {
      Serial.println("\nTime ready");
      return true;
    }
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nTime sync timeout");
  return false;
}

bool refreshCurrentTime() {
  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 10)) {
    currentTimeInfo = timeInfo;
    clockHasValidTime = true;
    return true;
  }
  return false;
}

void attemptTimeRecovery() {
  if (ssid[0] == '\0') return;

  unsigned long now = millis();
  if ((now - lastTimeRecoveryAttempt) < syncRetryInterval) return;
  lastTimeRecoveryAttempt = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Retrying WiFi...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    return;
  }

  configTzTime(tzInfo, ntpServer1, ntpServer2);
  struct tm timeInfo;
  if (waitForTimeSync(timeInfo, 1500)) {
    currentTimeInfo = timeInfo;
    clockHasValidTime = true;
    initializeInvaders(timeInfo.tm_min);
    lastMinute = timeInfo.tm_min;
    syncDisplayedScoreToCurrentTime();
    drawScreen();
  }
}

//----------------------------------------
// Effects
//----------------------------------------
void startScreenFlash() {
  flashActive = true;
  flashFrame = 0;
  lastFlashTick = millis();
  tft.fillScreen(ILI9341_WHITE);
}

void updateScreenFlash() {
  if (!flashActive) return;

  unsigned long now = millis();
  if ((now - lastFlashTick) < flashFrameInterval) return;

  lastFlashTick = now;
  flashFrame++;

  if (flashFrame >= 6) {
    flashActive = false;
    drawScreen();
    return;
  }

  tft.fillScreen((flashFrame % 2 == 0) ? ILI9341_WHITE : ILI9341_BLACK);
}

void startExplosion(int x, int y) {
  explosionX = x + 5 - 6;
  explosionY = y + 5 - 3;
  explosionFrame = 0;
  explosionActive = true;
  lastExplosionTick = millis();
  drawExplosionFrameAt(explosionX, explosionY, explosionColors[explosionFrame]);
}

void updateExplosion() {
  if (!explosionActive) return;

  unsigned long now = millis();
  if ((now - lastExplosionTick) < explosionFrameInterval) return;

  clearExplosionAt(explosionX, explosionY);
  lastExplosionTick = now;
  explosionFrame++;

  if (explosionFrame >= 3) {
    explosionActive = false;
    explosionX = -1;
    explosionY = -1;
    return;
  }

  drawExplosionFrameAt(explosionX, explosionY, explosionColors[explosionFrame]);
}

void resetTransientState() {
  unsigned long now = millis();

  missileX = -1;
  missileY = -1;
  missileImpactPending = false;
  invMissileX = -1;
  invMissileY = -1;
  invMissileTargetX = -1;
  targetX = -1;
  topMinuteFire = false;
  cannonAligning = false;
  cannonDodging = false;
  dodgeTargetX = -1;
  pendingDestructiveShot = false;
  pendingScoreUpdates = 0;
  endOfWavePending = false;
  endOfWaveResetAt = 0;

  explosionActive = false;
  explosionX = -1;
  explosionY = -1;

  flashActive = false;
  flashFrame = 0;

  lastInvaderShot = now;
  lastNonDestructiveAttempt = now;
}

void syncDisplayedScoreToCurrentTime() {
  displayedScoreHour = currentTimeInfo.tm_hour;
  displayedScoreMinute = currentTimeInfo.tm_min;
  scoreInitialized = true;
  pendingScoreUpdates = 0;
}

void advanceDisplayedScore() {
  if (!scoreInitialized) {
    syncDisplayedScoreToCurrentTime();
    return;
  }

  displayedScoreMinute++;
  if (displayedScoreMinute >= 60) {
    displayedScoreMinute = 0;
    displayedScoreHour = (displayedScoreHour + 1) % 24;
  }
}

//----------------------------------------
// UFO
//----------------------------------------
void triggerUFO() {
  if (!ufoActive) {
    ufoX = -UFO_WIDTH;
    ufoActive = true;
  }
}

void drawUFOAt(int x, int y, uint16_t color) {
  for (int row = 0; row < UFO_HEIGHT; row++) {
    for (int col = 0; col < UFO_WIDTH; col++) {
      if (ufoSprite[row][col]) {
        drawPixelSafe(x + col, y + row, color);
      }
    }
  }
}

void updateUFO() {
  if (!ufoActive) return;

  drawUFOAt(ufoX, ufoY, ILI9341_BLACK);
  ufoX += 1;

  if (ufoX > SCREEN_WIDTH) {
    ufoActive = false;
    ufoX = -1;
    return;
  }

  drawUFOAt(ufoX, ufoY, ILI9341_MAGENTA);
}

//----------------------------------------
// Setup / Loop
//----------------------------------------
void setup() {
  Serial.begin(115200);
  randomSeed(micros());

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(0);

  initializeInvaders(0);
  drawScreen();

  if (connectWiFiWithTimeout(wifiConnectTimeout)) {
    configTzTime(tzInfo, ntpServer1, ntpServer2);

    struct tm timeInfo;
    if (waitForTimeSync(timeInfo, ntpSyncTimeout)) {
      currentTimeInfo = timeInfo;
      clockHasValidTime = true;
      initializeInvaders(timeInfo.tm_min);
      lastMinute = timeInfo.tm_min;
      syncDisplayedScoreToCurrentTime();
    }
  }

  drawScreen();
}

void loop() {
  refreshCurrentTime();
  if (!clockHasValidTime) attemptTimeRecovery();

  if (clockHasValidTime) {
    if (lastMinute == -1) {
      initializeInvaders(currentTimeInfo.tm_min);
      lastMinute = currentTimeInfo.tm_min;
      syncDisplayedScoreToCurrentTime();
      drawScreen();
    } else if (currentTimeInfo.tm_min != lastMinute) {
      lastMinute = currentTimeInfo.tm_min;
      pendingScoreUpdates++;
      pendingDestructiveShot = true;

      if (currentTimeInfo.tm_min == 0) {
        endOfWavePending = true;
      } else {
        if ((currentTimeInfo.tm_min % 15) == 0) triggerUFO();
      }
    }
  }

  if (flashActive) {
    updateScreenFlash();
    delay(30);
    return;
  }

  unsigned long now = millis();

  if (endOfWaveResetAt != 0 && now >= endOfWaveResetAt) {
    resetTransientState();
    initializeInvaders(0);
    startScreenFlash();
    triggerUFO();
    delay(30);
    return;
  }

  if (pendingDestructiveShot && endOfWaveResetAt == 0 &&
      !topMinuteFire && !cannonAligning && !cannonDodging && missileY == -1) {
    setDestructiveTarget();
  } else if (!pendingDestructiveShot && !endOfWavePending && endOfWaveResetAt == 0 &&
             !topMinuteFire && !cannonAligning && !cannonDodging && missileY == -1) {
    if ((now - lastNonDestructiveAttempt) >= nonDestructiveInterval) {
      setNonDestructiveTarget();
      lastNonDestructiveAttempt = now;
    }
  }

  if ((now - lastFrameSwitch) >= frameSwitchInterval) {
    lastFrameSwitch = now;
    invaderFrame = !invaderFrame;
    drawInvaders();
    if (missileY > -1) drawMissile(missileX, missileY);
    if (invMissileY > -1) drawInvaderMissileAt(invMissileX, invMissileY, ILI9341_CYAN);
    if (explosionActive) drawExplosionFrameAt(explosionX, explosionY, explosionColors[explosionFrame]);
  }

  handleCannonMovement();
  updateMissile();
  updateInvaderMissile();
  updateExplosion();
  updateUFO();

  delay(30);
}

//----------------------------------------
// Game Logic
//----------------------------------------
void initializeInvaders(int minutesElapsed) {
  minutesElapsed = constrain(minutesElapsed, 0, ROWS * COLS);

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      invaders[r][c] = true;
    }
  }

  int destroyed = 0;
  for (int row = ROWS - 1; row >= 0 && destroyed < minutesElapsed; row--) {
    for (int col = COLS - 1; col >= 0 && destroyed < minutesElapsed; col--) {
      invaders[row][col] = false;
      destroyed++;
    }
  }
}

void setDestructiveTarget() {
  int invXCenter;
  int invY;

  if (findLastInvaderToDestroy(invXCenter, invY)) {
    targetX = invXCenter;
    topMinuteFire = true;
    cannonAligning = true;
    pendingDestructiveShot = false;
  } else {
    pendingDestructiveShot = false;
    if (endOfWavePending && endOfWaveResetAt == 0) {
      endOfWaveResetAt = millis() + endOfWaveResetDelay;
    }
  }
}

int getGapCenter(int gapIndex) {
  return INVADER_LEFT_MARGIN + INVADER_WIDTH + (INVADER_X_SPACING / 2) +
         gapIndex * (INVADER_WIDTH + INVADER_X_SPACING);
}

int chooseInvaderMissTargetX() {
  int cannonCenter = cannonX + (BASE_WIDTH / 2);
  return constrain(cannonCenter + random(-4, 5), invaderMissMinX, invaderMissMaxX);
}

int getInvaderMissileDrawX(int missileBaseX, int missileY) {
  bool phase = (missileY / 4) % 2;
  return phase ? missileBaseX + 2 : missileBaseX - 2;
}

bool invaderMissileThreatensCannon(int missileBaseX, int missileY) {
  int cannonTop = SCREEN_HEIGHT - BASE_HEIGHT - 10;
  if (missileY < (cannonTop - invaderMissRedirectZone)) return false;

  int drawX = getInvaderMissileDrawX(missileBaseX, missileY);
  int safeLeft = cannonX - invaderMissSafetyMargin;
  int safeRight = cannonX + BASE_WIDTH + invaderMissSafetyMargin;

  return drawX >= safeLeft && drawX <= safeRight;
}

void startCannonDodge(int missileBaseX, int missileY) {
  int drawX = getInvaderMissileDrawX(missileBaseX, missileY);
  int leftTarget = constrain(drawX - BASE_WIDTH - invaderMissSafetyMargin - 2,
                             0, SCREEN_WIDTH - BASE_WIDTH);
  int rightTarget = constrain(drawX + invaderMissSafetyMargin + 2,
                              0, SCREEN_WIDTH - BASE_WIDTH);

  bool canGoLeft = (leftTarget + BASE_WIDTH) < (drawX - 1);
  bool canGoRight = rightTarget > (drawX + 1);

  if (canGoLeft && canGoRight) {
    dodgeTargetX = (abs(cannonX - leftTarget) <= abs(cannonX - rightTarget)) ? leftTarget : rightTarget;
  } else if (canGoLeft) {
    dodgeTargetX = leftTarget;
  } else if (canGoRight) {
    dodgeTargetX = rightTarget;
  } else {
    dodgeTargetX = (drawX < (SCREEN_WIDTH / 2)) ? (SCREEN_WIDTH - BASE_WIDTH) : 0;
  }

  cannonDodging = true;
}

void setNonDestructiveTarget() {
  targetX = getGapCenter(random(0, COLS - 1));
  topMinuteFire = false;
  cannonAligning = true;
}

bool findLastInvaderToDestroy(int &invXCenter, int &invY) {
  for (int row = ROWS - 1; row >= 0; row--) {
    for (int col = COLS - 1; col >= 0; col--) {
      if (invaders[row][col]) {
        int invX = INVADER_LEFT_MARGIN + col * (INVADER_WIDTH + INVADER_X_SPACING);
        int invYPos = INVADER_TOP_MARGIN + row * (INVADER_HEIGHT + INVADER_Y_SPACING);
        invXCenter = invX + (INVADER_WIDTH / 2);
        invY = invYPos;
        return true;
      }
    }
  }
  return false;
}

bool anyInvadersAlive() {
  for (int row = 0; row < ROWS; row++) {
    for (int col = 0; col < COLS; col++) {
      if (invaders[row][col]) return true;
    }
  }
  return false;
}

void fireMissileIfNeeded() {
  if (missileY == -1) fireMissile();
}

void fireMissile() {
  missileX = cannonX + (BASE_WIDTH / 2);
  missileY = SCREEN_HEIGHT - BASE_HEIGHT - MISSILE_HEIGHT - 10;
  drawMissile(missileX, missileY);
}

void updateMissile() {
  if (missileY == -1) return;

  if (missileImpactPending) {
    eraseMissile(missileX, missileY);
    if (!destroyOneInvaderAtMissile() && topMinuteFire) {
      pendingDestructiveShot = (pendingScoreUpdates > 0);
    }
    missileX = -1;
    missileY = -1;
    missileImpactPending = false;
    topMinuteFire = false;
    return;
  }

  eraseMissile(missileX, missileY);
  missileY -= 3;

  if (missileY <= 46) {
    missileX = -1;
    missileY = -1;
    missileImpactPending = false;
    if (topMinuteFire) {
      pendingDestructiveShot = (pendingScoreUpdates > 0);
    }
    topMinuteFire = false;
    return;
  }

  drawMissile(missileX, missileY);

  if (topMinuteFire) {
    int hitRow;
    int hitCol;
    if (findInvaderAtMissile(hitRow, hitCol)) {
      missileImpactPending = true;
    }
  }
}

void updateInvaderMissile() {
  unsigned long now = millis();

  if (invMissileY == -1 && (now - lastInvaderShot) >= invaderShotInterval) {
    int aliveCount = 0;
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        if (invaders[r][c]) aliveCount++;
      }
    }

    if (aliveCount > 0) {
      int pick = random(0, aliveCount);
      int idx = 0;
      bool found = false;

      for (int r = 0; r < ROWS && !found; r++) {
        for (int c = 0; c < COLS; c++) {
          if (!invaders[r][c]) continue;
          if (idx == pick) {
            invMissileX = INVADER_LEFT_MARGIN + c * (INVADER_WIDTH + INVADER_X_SPACING) +
                          (INVADER_WIDTH / 2);
            invMissileY = INVADER_TOP_MARGIN + r * (INVADER_HEIGHT + INVADER_Y_SPACING) +
                          INVADER_HEIGHT;
            invMissileTargetX = chooseInvaderMissTargetX();
            lastInvaderShot = now;
            found = true;
            break;
          }
          idx++;
        }
      }
    }
  }

  if (invMissileY == -1) return;

  eraseInvaderMissile(invMissileX, invMissileY);

  invMissileY += 2;
  if (invMissileX < invMissileTargetX) invMissileX++;
  else if (invMissileX > invMissileTargetX) invMissileX--;

  if (invaderMissileThreatensCannon(invMissileX, invMissileY)) {
    startCannonDodge(invMissileX, invMissileY);
  }

  if (invMissileY >= SCREEN_HEIGHT) {
    eraseInvaderMissile(invMissileX, invMissileY);
    invMissileX = -1;
    invMissileY = -1;
    invMissileTargetX = -1;
    return;
  }

  drawInvaderMissileAt(invMissileX, invMissileY, ILI9341_CYAN);
}

bool findInvaderAtMissile(int &hitRow, int &hitCol) {
  // Use only the leading tip of the missile so the visible impact lines up
  // with the hit event and score update.
  const int missileTipHeight = 2;
  int mLeft = missileX - (MISSILE_WIDTH / 2);
  int mRight = mLeft + MISSILE_WIDTH - 1;
  int mTop = missileY;
  int mBottom = missileY + missileTipHeight - 1;

  for (int row = 0; row < ROWS; row++) {
    for (int col = 0; col < COLS; col++) {
      if (!invaders[row][col]) continue;

      int iX = INVADER_LEFT_MARGIN + col * (INVADER_WIDTH + INVADER_X_SPACING);
      int iY = INVADER_TOP_MARGIN + row * (INVADER_HEIGHT + INVADER_Y_SPACING);
      int iX2 = iX + INVADER_WIDTH - 1;
      int iY2 = iY + INVADER_HEIGHT - 1;

      if (!(iX2 < mLeft || iX > mRight || iY2 < mTop || iY > mBottom)) {
        hitRow = row;
        hitCol = col;
        return true;
      }
    }
  }
  return false;
}

bool destroyOneInvaderAtMissile() {
  int hitRow;
  int hitCol;
  if (!findInvaderAtMissile(hitRow, hitCol)) return false;

  int iX = INVADER_LEFT_MARGIN + hitCol * (INVADER_WIDTH + INVADER_X_SPACING);
  int iY = INVADER_TOP_MARGIN + hitRow * (INVADER_HEIGHT + INVADER_Y_SPACING);

  invaders[hitRow][hitCol] = false;
  startExplosion(iX, iY);

  if (pendingScoreUpdates > 0) {
    pendingScoreUpdates--;
    advanceDisplayedScore();
    drawScore();
  }

  if (endOfWavePending) {
    if (anyInvadersAlive()) {
      pendingDestructiveShot = (pendingScoreUpdates > 0);
    } else if (endOfWaveResetAt == 0) {
      endOfWaveResetAt = millis() + endOfWaveResetDelay;
    }
  } else {
    pendingDestructiveShot = (pendingScoreUpdates > 0);
  }

  return true;
}

//----------------------------------------
// Drawing
//----------------------------------------
void drawPixelSafe(int x, int y, uint16_t color) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
  tft.drawPixel(x, y, color);
}

void drawScreen() {
  tft.fillScreen(ILI9341_BLACK);
  drawScore();
  drawInvaders();
  drawCannon();
  if (missileY > -1) drawMissile(missileX, missileY);
  if (invMissileY > -1) drawInvaderMissileAt(invMissileX, invMissileY, ILI9341_CYAN);
  if (explosionActive) drawExplosionFrameAt(explosionX, explosionY, explosionColors[explosionFrame]);
  if (ufoActive) drawUFOAt(ufoX, ufoY, ILI9341_MAGENTA);
}

void drawScore() {
  tft.fillRect(0, 0, SCREEN_WIDTH, 45, ILI9341_BLACK);

  const int rightEdge = SCREEN_WIDTH - 10;
  const int hiLabelX = rightEdge - (8 * 6);
  const int hiValueX = rightEdge - (6 * 12);

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 8);
  tft.print("SCORE");
  tft.setCursor(hiLabelX, 8);
  tft.print("HI-SCORE");

  tft.setTextSize(2);

  if (scoreInitialized) {
    char scoreStr[5];
    snprintf(scoreStr, sizeof(scoreStr), "%02d%02d", displayedScoreHour, displayedScoreMinute);
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(10, 22);
    tft.print(scoreStr);
  } else {
    tft.setTextColor(0x0320);
    tft.setCursor(10, 22);
    tft.print("----");
  }

  if (clockHasValidTime) {
    char dateStr[7];
    snprintf(dateStr, sizeof(dateStr), "%02d%02d%02d",
             currentTimeInfo.tm_year % 100,
             currentTimeInfo.tm_mon + 1,
             currentTimeInfo.tm_mday);
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(hiValueX, 22);
    tft.print(dateStr);
  } else {
    tft.setTextColor(0x8000);
    tft.setCursor(hiValueX, 22);
    tft.print("NOSYNC");
  }

  tft.drawFastHLine(0, 44, SCREEN_WIDTH, 0x4208);
}

void drawInvaders() {
  for (int row = 0; row < ROWS; row++) {
    for (int col = 0; col < COLS; col++) {
      int x = INVADER_LEFT_MARGIN + col * (INVADER_WIDTH + INVADER_X_SPACING);
      int y = INVADER_TOP_MARGIN + row * (INVADER_HEIGHT + INVADER_Y_SPACING);

      if (invaders[row][col]) {
        const uint8_t (*sprite)[10] = invaderFrame ? invaderSpritesB[row] : invaderSpritesA[row];
        drawSpriteFromArray(x, y, invaderColors[row], sprite);
      } else {
        tft.fillRect(x, y, INVADER_WIDTH, INVADER_HEIGHT, ILI9341_BLACK);
      }
    }
  }
}

void drawCannon() {
  drawScaledSprite(cannonX, SCREEN_HEIGHT - BASE_HEIGHT - 10,
                   BASE_WIDTH, BASE_HEIGHT, ILI9341_WHITE, cannonSprite);
}

void handleCannonMovement() {
  tft.fillRect(cannonX, SCREEN_HEIGHT - BASE_HEIGHT - 10,
               BASE_WIDTH, BASE_HEIGHT, ILI9341_BLACK);

  if (cannonDodging && dodgeTargetX != -1) {
    if (cannonX < dodgeTargetX) cannonX += cannonDodgeSpeed;
    else if (cannonX > dodgeTargetX) cannonX -= cannonDodgeSpeed;

    cannonX = constrain(cannonX, 0, SCREEN_WIDTH - BASE_WIDTH);

    if (invMissileY == -1 || !invaderMissileThreatensCannon(invMissileX, invMissileY)) {
      cannonDodging = false;
      dodgeTargetX = -1;
    }
  } else if (cannonAligning && targetX != -1) {
    int center = cannonX + (BASE_WIDTH / 2);
    if (center < targetX) cannonX += 4;
    else if (center > targetX) cannonX -= 4;

    cannonX = constrain(cannonX, 0, SCREEN_WIDTH - BASE_WIDTH);

    if (abs((cannonX + (BASE_WIDTH / 2)) - targetX) <= 4) {
      fireMissileIfNeeded();
      cannonAligning = false;
      targetX = -1;
    }
  } else {
    cannonX += cannonDirection * 2;
    cannonX = constrain(cannonX, 0, SCREEN_WIDTH - BASE_WIDTH);

    if (cannonX <= 0) cannonDirection = 1;
    if (cannonX >= SCREEN_WIDTH - BASE_WIDTH) cannonDirection = -1;
  }

  drawCannon();
}

void drawMissile(int x, int y) {
  tft.fillRect(x - (MISSILE_WIDTH / 2), y, MISSILE_WIDTH, MISSILE_HEIGHT, ILI9341_WHITE);
}

void drawInvaderMissileAt(int x, int y, uint16_t color) {
  bool phase = (y / 4) % 2;
  int drawX = phase ? x + 2 : x - 2;

  drawPixelSafe(drawX, y, color);
  drawPixelSafe(drawX, y + 1, color);
  drawPixelSafe(drawX, y + 2, color);
  drawPixelSafe(drawX, y + 3, color);
}

uint16_t getBackgroundColor(int px, int py) {
  if (px < 0 || px >= SCREEN_WIDTH || py < 0 || py >= SCREEN_HEIGHT) return ILI9341_BLACK;

  for (int row = 0; row < ROWS; row++) {
    for (int col = 0; col < COLS; col++) {
      if (!invaders[row][col]) continue;

      int ix = INVADER_LEFT_MARGIN + col * (INVADER_WIDTH + INVADER_X_SPACING);
      int iy = INVADER_TOP_MARGIN + row * (INVADER_HEIGHT + INVADER_Y_SPACING);

      if (px >= ix && px < ix + INVADER_WIDTH && py >= iy && py < iy + INVADER_HEIGHT) {
        const uint8_t (*sprite)[10] = invaderFrame ? invaderSpritesB[row] : invaderSpritesA[row];
        return sprite[py - iy][px - ix] ? invaderColors[row] : ILI9341_BLACK;
      }
    }
  }

  return ILI9341_BLACK;
}

void eraseMissile(int x, int y) {
  int mLeft = x - (MISSILE_WIDTH / 2);
  for (int py = y; py < y + MISSILE_HEIGHT; py++) {
    for (int px = mLeft; px < mLeft + MISSILE_WIDTH; px++) {
      tft.drawPixel(px, py, getBackgroundColor(px, py));
    }
  }
}

void eraseInvaderMissile(int x, int y) {
  bool phase = (y / 4) % 2;
  int drawX = phase ? x + 2 : x - 2;

  drawPixelSafe(drawX, y, getBackgroundColor(drawX, y));
  drawPixelSafe(drawX, y + 1, getBackgroundColor(drawX, y + 1));
  drawPixelSafe(drawX, y + 2, getBackgroundColor(drawX, y + 2));
  drawPixelSafe(drawX, y + 3, getBackgroundColor(drawX, y + 3));
}

void drawExplosionFrameAt(int x, int y, uint16_t color) {
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 13; col++) {
      if (explosionSprite[row][col]) {
        drawPixelSafe(x + col, y + row, color);
      }
    }
  }
}

void clearExplosionAt(int x, int y) {
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 13; col++) {
      if (explosionSprite[row][col]) {
        int px = x + col;
        int py = y + row;
        drawPixelSafe(px, py, getBackgroundColor(px, py));
      }
    }
  }
}

void drawSpriteFromArray(int x, int y, uint16_t color, const uint8_t sprite[10][10]) {
  for (int yy = 0; yy < 10; yy++) {
    for (int xx = 0; xx < 10; xx++) {
      tft.drawPixel(x + xx, y + yy, sprite[yy][xx] ? color : ILI9341_BLACK);
    }
  }
}

void drawScaledSprite(int x, int y, int w, int h, uint16_t color, const uint8_t sprite[10][10]) {
  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      tft.drawPixel(x + xx, y + yy,
                    sprite[(yy * 10) / h][(xx * 10) / w] ? color : ILI9341_BLACK);
    }
  }
}
