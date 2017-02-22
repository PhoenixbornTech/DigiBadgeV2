//DigiBadge V2 code by Jason "Andon" LeClare
//Includes code from Adafruit ST7735 examples
//Code Version 1.1
//
//Basic functions include three modes:
//Mode One: Color Badge. Switches between Red/Green/Yellow badges.
//Mode Two: Slideshow. Continually cycles through images found on the SD card.
//Mode Three: Selectable images. Displays single image from SD card, and switches between.
//
//If no SD card is detected, then Modes Two and Three will be disabled.
//If only one image is detected on the SD card, then Mode Two will be disabled.
//
//The ATMega328 microcontroller has a 1.8v minimum voltage selected, but at voltages that low,
//the screen's backlight is useless. I've programmed in a low battery warning at about 2.65v,
//and the ATMega328 will enter a 'permanent' sleep mode at 2.5v. This sleep mode will only be
//broken if the device is reset - But if the battery voltage is too low, then it'll just go
//right back to sleep.
//
//The program detects insertion and removal of SD cards, and will load and re-load accordingly.
//When an SD card is loaded, or if the badge is started with an SD card loaded, it will find the
//first MaxFiles (set below) number of .bmp files and attempt to load them.
//It currently won't check if the file is the proper type of bmp file (24-bit), so a wrong bmp
//will fail to load.
//
//Additionally, inserting an SD card into the socket (sometimes) causes the device to reset. I'm fairly sure
//that this is caused by a power spike, but time constraints made it impractical to fix before
//BronyCon. The hotswap functionality remains because on a few of the prototypes it has worked just fine.
//Some tweaking will be needed for the v2.1 boards.
//
//By default, MaxFiles is set to 18 files, at 13 character length (4 characters for file extension,
//8 for name, and one for null terminator). Increasing MaxFiles increases global variable usage
//which is already pretty high (74%). In my tests, around 75-80% memory usage makes the badge do
//really weird things, starting with not loading images properly and continuing on to errant reading
//of stick inputs. I don't know if the SD card is safe in such conditions.
//
//Theoretically, you could remove the extension from the filename list as we already know they're bmp.
//This would increase the maximum number of files loadable for the same space - 260 characters is used
//currently for 20 files at 13 characters. 29 files at 9 characters takes 261, and 30 uses 270.
//This is one of my goals for the program, but currently not a super high priority.
//
//And speaking of memory efficiency - There are numerous Serial calls throught the program.
//They have all been commented out. Serial calls use up quite a lot of global variable usage,
//even if they're static. If you need to debug something, turn on the serial calls where needed.
//But remember that if they're all on, the program won't run as the memory usage will go abouve
//the 75%-80% mark.
//
//This version of the code is the "Launch Ready" code. It's what I'll be loading up onto the V2s
//that I'll be selling at BronyCon.
//
//This wall of commentary was updated on July 4, 2016 by Andon.
//Happy hacking!

#include <avr/sleep.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library
#include <SPI.h>
#include <SD.h>

#define SD_CS    10   // Chip select line for SD card
#define SD_CD    7    // Card Detect line for SD card
#define TFT_CS   9    // Chip select line for TFT display
#define TFT_DC   8    // Data/command line for TFT
#define TFT_RST  5    // Reset line for TFT (or connect to +5V)
#define MaxFiles 18   // Maximum number of files to load.
#define FileLen  13   // Maximum length of file name
#define TFT_DIM  6    // TFT Backlight dimmer
#define S_UP     A1   // Navigation stick up/Increase brightness
#define S_DOWN   4    // Navigation stick down/Dcecrease Brightness
#define S_LEFT   A0   // Navigation stick left (Facing back) - Change badge/image
#define S_RIGHT  2    // Navigation stick right (Facing back) - Change badge/image
#define S_SEL    3   // Navigation stick select - Change mode
#define STEPSPD  5000 // MS between slideshow changes. Default is 5 seconds.
#define LOWBATT  2650 // millivolts at which the low battery alarm shows up
#define POWOFF   2500 // millivolts at which the board turns to a low-power state.

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

//int fnum = 0;
//char filelist[MaxFiles][FileLen];
bool SDInit = false;
int filecount = 0; //Number of discovered images.
int curimg = 1;
int mode = 0; //0 is Badge, 1 is Slideshow, 2 is Select Image
int badge = 0; // 0 is Yellow, 1 is Red, 2 is Green
int bright = 13; //Brightness from 0-25, auto-set in middle.
int steps = 0; //To determine when to change slideshow image.
int image = 0;
long vcc = 0;
bool lowbat = false;

void setup()
{
  //Serial.begin(9600);
  //Serial.println("Digibadge Starting");
  //Serial.println("Debugging Serial mode enabled.");
  //Start up screen functions.
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setCursor(0, 0);
  tft.print(F("DigiBadge V2 Initializing."));
  //Don't turn the light on until screen has started.
  //This will prevent seeing any left over image artifacts.
  pinMode(TFT_DIM, OUTPUT);
  setLight(bright);
  tft.setCursor(0, 16);
  tft.print(F("Checking SD Card..."));
  pinMode(SD_CD, INPUT_PULLUP);
  tft.setCursor(0, 24);
  SDInit = startSD();
  tft.setCursor(0, 32);
  //Initialize the navigation stick.
  tft.print("Initializing stick");
  pinMode(S_UP, INPUT_PULLUP);
  pinMode(S_DOWN, INPUT_PULLUP);
  pinMode(S_LEFT, INPUT_PULLUP);
  pinMode(S_RIGHT, INPUT_PULLUP);
  pinMode(S_SEL, INPUT_PULLUP);
  tft.setCursor(0, 40);
  delay(100);
  tft.print(F("Stick initialized"));
  delay(100);
  //Check battery voltage.
  vcc = readVcc();
  if (vcc < LOWBATT) {
    //Battery voltage < 2.2v
    //Single AAA cell drops sharply at 1.1v, and we have two in series.
    //Battery depletion is imminent, as BOD kicks in at 1.8v
    lowbat = true;
  }
  //Get the full volts.
  int v1 = vcc / 1000;
  //Remove the full volts, then get the partial volts.
  //Dividing by 100 turns this into a single decimal, IE 2.9v
  int v2 = (vcc - (v1 * 1000)) / 100;
  tft.setCursor(0, 54);
  tft.print(F("Battery: "));
  if (lowbat) {
    //If our battery is low, print the battery voltage in red.
    tft.setTextColor(ST7735_RED);
  }
  tft.print(v1);
  tft.print(F("."));
  tft.print(v2);
  //Revert to normal text color
  tft.setTextColor(ST7735_WHITE);
  tft.print(F("v"));
  //Print information.
  tft.setCursor(0, 62);
  tft.print(F("For code & schematics"));
  tft.setCursor(0, 70);
  tft.print(F("visit www.matchfire.net"));
  delay(100);
  tft.setCursor(0, 86);
  tft.print(F("Created by Jason LeClare"));
  tft.setCursor(0, 94);
  tft.print(F("2016"));
  delay(2500);
  //Initial badge or image draw
  //By default this will be the Yellow Badge
  //But if Mode, Image, and/or Badge are changed above
  //Then this will change.
  if (mode == 0) {
    drawBadge(badge);
  }
  else {
    //bmpDraw(filelist[image], 0, 0);
    newDrawBMP(curimg, 0, 0);
  }
}

void loop() {
  //Check the battery voltage.
  //This won't work if we're powered via FTDI as it pulls VCC voltage
  //Generally, if we're powered via FTDI, battery voltage is irrelevant.
  vcc = readVcc(); //Returns the millivoltage of the batteries.
  if (vcc < POWOFF) {
    //We're at a battery voltage where it's not useful to remain powered.
    //Turn off the screen backlight
    analogWrite(TFT_DIM, 0);
    //Set our mode to Badge mode to ensure we don't try to access SD card.
    mode = 0;
    //Usually we'd draw a badge, but the screen is off so that's irrelevant.
    //Instead, we'll put the device in a "sleep" mode that'll render it inert
    //until powered off and back on.
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_mode();
  }
  if ((vcc < LOWBATT) and (lowbat == false)) {
    //Same low battery check as startup, except ignored if lowbat is already set.
    lowbat = true;
  }
  else if ((vcc > (LOWBATT + 100)) and (lowbat == true)) {
    //If we previously had a low battery and the voltage is now much higher,
    //Change lowbat to false. So that we don't get rapid switches
    //with voltage fluctuation, this is a bit higher than the lowbat threshold.
    lowbat = false;
  }
  //Now, check if we have inserted an SD card
  int SDCard = digitalRead(SD_CD);
  if ((SDInit) && (SDCard == 1)) {
    //SDCard has been removed.
    SDInit = false;
    //Set mode to Badge mode.
    tft.fillScreen(ST7735_BLACK);
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(0, 0);
    tft.print(F("SD Card Removed"));
    tft.setCursor(0, 8);
    tft.print(F("Returning to badge mode..."));
    delay(1000);
    mode = 0; //Set the mode
    drawBadge(badge);
  }
  if ((! SDInit) && (SDCard == 0)) {
    //We have an uninitialized SD card.
    //Initialize it.
    //NOTE: This will only work ONCE, unless
    //a modification is made to SD.cpp
    //as per https://github.com/arduino/Arduino/issues/3607
    //All DigiBadge V2 will have this fix incorporated
    //before distribution or sale.
    tft.fillScreen(ST7735_BLACK);
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(0, 0);
    tft.print(F("New SD card found."));
    tft.setCursor(0, 8);
    tft.print(F("Checking SD Card..."));
    tft.setCursor(0, 16);
    SDInit = startSD();
    tft.setCursor(0, 24);
    tft.print(F("Returning to Badge mode..."));
    //Wait so we can actually read it.
    delay(1000);
    drawBadge(badge);
  }
  //Now go to command reading.
  int up = digitalRead(S_UP);
  int down = digitalRead(S_DOWN);
  int right = digitalRead(S_RIGHT);
  int left = digitalRead(S_LEFT);
  int sel = digitalRead(S_SEL);
  if (up == LOW) {
    //Increase brightness, to max of 10.
    //Serial.println("Increasing brightness");
    if (bright < 25) {
      bright ++;
      setLight(bright);
    }
  }
  else if (down == LOW) {
    //Decrease brightness, to min of 1)
    //Serial.println("Decreasing Brightness");
    if (bright > 0) {
      bright --;
      setLight(bright);
    }
  }
  if (sel == LOW) {
    //Change mode.
    //Check if we have an SD card
    if (SDCard == 1) {
      //No SDCard found
      tft.fillScreen(ST7735_BLACK);
      tft.setCursor(0, 0);
      tft.setTextColor(ST7735_WHITE);
      tft.print(F("No SD card found."));
      tft.setCursor(0, 8);
      tft.print(F("Returning to badge mode..."));
      delay(1000);
    }
    //Serial.println("Changing mode");
    if ((mode < 2) && (SDCard != 1)) {
      mode ++;
    }
    else {
      //default to Badge Mode
      mode = 0;
    }
    //Draw the appropriate changes.
    if (mode == 0) {
      //Badge mode. Draw the appropriate badge.
      drawBadge(badge);
    }
    else {
      //Slideshow or Image mode.
      steps = 0; //Reset the counter.
      newDrawBMP(curimg, 0, 0);
      //bmpDraw(filelist[image], 0, 0);
    }
  }
  if (left == LOW) {
    //Depending on mode
    //In Slideshow or Image mode, shows previous image.
    //In Badge mode, shows previous badge.
    if (mode == 0) {
      //Badge Mode
      //Serial.println("Previous Badge");
      if (badge > 0) {
        badge --;
      }
      else {
        badge = 2;
      }
    }
    else {
      //Slideshow or Image mode
      //Serial.println("Previous Image");
      /*if (image > 0) {
        image --;
      }
      else {
        image = fnum - 1;
      }*/
      if (curimg > 1) {
        curimg --;
      }
      else {
        curimg = filecount;
      }
    }
    //Draw the appropriate changes.
    if (mode == 0) {
      //Badge mode. Draw the appropriate badge.
      drawBadge(badge);
    }
    else {
      //Slideshow or Image mode.
      steps = 0; //Reset the counter.
      //bmpDraw(filelist[image], 0, 0);
      newDrawBMP(curimg, 0, 0);
    }
  }
  if (right == LOW) {
    //As left, but next image/badge
    if (mode == 0) {
      //Badge mode
      //Serial.println("Next Badge");
      if (badge < 2) {
        badge ++;
      }
      else {
        badge = 0;
      }
    }
    else {
      //Slideshow or image mode
      //Serial.println("Next Image");
      /*if (image < (fnum - 1)) {
        image ++;
      }
      else {
        image = 0;
      }*/
      if (curimg < filecount){
        curimg ++;
      }
      else {
        curimg = 1;
      }
    }
    //Draw the appropriate changes.
    if (mode == 0) {
      //Badge mode. Draw the appropriate badge.
      drawBadge(badge);
    }
    else {
      //Slideshow or Image mode.
      steps = 0; //Reset the counter.
      //bmpDraw(filelist[image], 0, 0);
      newDrawBMP(curimg, 0, 0);
    }
  }
#define DelayTime 200
  delay(DelayTime);
  if (mode == 1) {
    steps ++;
    if ((steps * DelayTime) >= STEPSPD) {
      //Slideshow mode: Change images.
      /*if (image < fnum - 2) {
        image ++;
      }
      else {
        image = 0;
      }
      steps = 0;
      bmpDraw(filelist[image], 0, 0);*/
      //If we're less than the maximum, go to the next image.
      //Otherwise, reset to the first image
      if (curimg < filecount) {
        curimg ++;
      }
      else {
        curimg = 1;
      }
      steps = 0;
      newDrawBMP(curimg, 0, 0);
    }
  }
}

void setLight(int lt) {
  // Sets the backlight to a specified brightness
  analogWrite(TFT_DIM, (lt * 10) + 5);
  // AnalogWrite uses values from 0-255. We want the badge to always be visible, so set a minimum value of 5.
  // After that, there are 26 levels of brightness, at 10 each (Including one at 0).
}

void drawLowBat(int x, int y) {
  //Draws a low battery symbol with top-left at X,Y.
  //This is designed to be a bit intrusive
  //However, it IS designed to be a bit out of the way so it won't cover up a badge.
  //It fills a black background and adds the battery on top of that
  //To make the symbol visible on all badges and images.
  tft.fillRect(x, y, x + 22, y + 11, ST7735_BLACK);
  tft.fillRect(x + 2, y + 2, x + 16, y + 7, ST7735_RED);
  tft.fillRect(x + 18, y + 4, x + 2, y + 3, ST7735_RED);
  tft.drawLine(x + 8, y + 10, x + 13, y, ST7735_BLACK);
  tft.drawLine(x + 7, y + 10, x + 12, y, ST7735_BLACK);
}

void drawBadge(int b) {
  //Color Communication Badges
  //For more information, see
  //https://autisticadvocacy.org/wp-content/uploads/2014/02/ColorCommunicationBadges.pdf
  //
  //Set text size and color.
  tft.setTextSize(3);
  tft.setTextColor(ST7735_BLACK);
  if (b == 1) {
    //Red badge.
    tft.fillScreen(ST7735_RED);
    tft.fillRect(53, 18, 54, 54, ST7735_BLACK);
    tft.fillRect(56, 21, 48, 48, ST7735_WHITE);
    tft.setCursor(54, 85);
    tft.print(F("RED"));
  }
  else if (b == 2) {
    //Green badge
    tft.fillScreen(ST7735_GREEN);
    tft.fillCircle(80, 45, 27, ST7735_BLACK);
    tft.fillCircle(80, 45, 24, ST7735_WHITE);
    tft.setCursor(36, 85);
    tft.print(F("GREEN"));
  }
  else {
    //Yellow badge.
    //Default to this.
    tft.fillScreen(ST7735_YELLOW);
    tft.fillRect(22, 26, 116, 25, ST7735_BLACK);
    tft.fillRect(25, 29, 110, 19, ST7735_WHITE);
    tft.setCursor(28, 85);
    tft.print(F("YELLOW"));
  }
  //Set colors back to default.
  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  if (lowbat) {
    //If our battery is low, show the low battery symbol
    drawLowBat(0, 0);
  }
}

bool startSD() {
  //Serial.println("Detecting SD card...");
  int SDCard = digitalRead(SD_CD);
  if (SDCard == 1) {
    //Serial.println("No SD card found.");
    tft.print(F("No SD card found"));
    return false;
  }
  //Serial.println("SD Card found. Attempting load.");
  if (! SD.begin(SD_CS)) {
    //Serial.println("SD Card load failed.");
    tft.print(F("SD Card load failed"));
    return false;
  }
  //Serial.println("SD Card loaded successfully.");
  tft.print(F("SD Card loaded "));
  //Set the file number to 0. This will allow re-initializing of SD cards without adding files.
  //This will overwrite any old filenames, and even if there are fewer files, nothing beyond fnum
  //will be loaded.
  filecount = 0;
  //Serial.println("Loading list of BMP files.");
  newDrawBMP(-1, 0, 0);
  //Serial.print(filecount);
  tft.print(filecount);
  tft.print(F(" files."));
  //Serial.println(" Files found.");
  return true;
}

void newDrawBMP(int img, int x, int y) {
  //Either finds a file at located position
  //OR simply counts them, if img is -1
  int count = 0;
  File dir = SD.open("/"); //Open Directory
  dir.rewindDirectory(); //Rewind everything, because sometimes it gets funky.
  File fi = dir.openNextFile(); //Open the first file.
  while (fi) {
    //As long as we have a file open...
    if (! fi.isDirectory()) {
      //A file. Check if it is a BMP.
      if (String(fi.name()).endsWith(".BMP")) {
        //It's a BMP image
        count++; //Count it
        if ((count == img) && (img >= 1)) {
          //This is the file we're looking for.
          fi.close(); //Close it.
          //Serial.print(F("Loaded File: "));
          //Serial.println(fi.name());
          bmpDraw(fi.name(), x, y); //Display it.
          break; //No need to go searching anymore.
        }
      }
    }
    fi.close(); //Close the file, open the next.
    delay(10); //Short delay, just to be safe.
    fi = dir.openNextFile();
  }
  dir.close(); //Close the directory
  if (img < 1) {
    //We were counting files.
    //Serial.print(F("Updating filecount to "));
    filecount = count;
    //Serial.println(filecount);
  }
}

/*void listSDFiles() {
  File dir = SD.open("/");
  //Serial.println("Finding files on SD card");
  while (true) {
    File entry = dir.openNextFile();
    if (! entry) {
      //End of file list
      //Serial.println("End of file list.");
      dir.rewindDirectory();
      break;
    }
    if (! entry.isDirectory()) {
      //File is not a Directory
      //Serial.print("File ");
      //Serial.print(entry.name());
      //Serial.println(" found");
      String fname = String(entry.name());
      if (fname.endsWith(".BMP")) {
        //File is a bmp file. Check the length.
        if (sizeof(entry.name()) < FileLen) {
          //Add it to the list
          strcpy(filelist[fnum], entry.name());
          //Serial.println("File copied to list");
          fnum += 1;
          if (fnum >= MaxFiles) {
            //Serial.println("Maximum files reached.");
            break;
          }
        }
        else {
          delay(1);
          //Serial.println("File name length too long.");
          //Serial.println("File not added to list.");
        }
      }
    }
    entry.close();
    delay(10);
  }
  dir.close();
}*/

/*void listfiles(){
  //Commented out to save space.
  //Saved for debugging.
  //Display a list of all stored filenames.
  Serial.println("Files found:");
  for (int x = 0; x < fnum; x++) {
    Serial.println(filelist[x]);
  }
  }*/

#define BUFFPIXEL 20 //Pixel buffer. I haven't seen any performance gains from increasing this.
//Going beyond about 50 tends to make the whole thing just not work, as well.

void bmpDraw(char *filename, uint8_t x, uint8_t y) {

  File     bmpFile;
  int      bmpWidth, bmpHeight;   // W+H in pixels
  uint8_t  bmpDepth;              // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;        // Start of image data in file
  uint32_t rowSize;               // Not always = bmpWidth; may have padding
  uint8_t  sdbuffer[3 * BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
  uint8_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
  boolean  goodBmp = false;       // Set to true on valid header parse
  boolean  flip    = true;        // BMP is stored bottom-to-top
  int      w, h, row, col;
  uint8_t  r, g, b;
  uint32_t pos = 0, startTime = millis();

  if ((x >= tft.width()) || (y >= tft.height())) return;

  //Serial.println();
  //Serial.print("Loading image '");
  //Serial.print(filename);
  //Serial.println('\'');

  // Open requested file on SD card
  if ((bmpFile = SD.open(filename)) == NULL) {
    //Serial.print("File not found");
    return;
  }

  // Parse BMP header
  if (read16(bmpFile) == 0x4D42) { // BMP signature
    //Serial.print("File size: "); Serial.println(read32(bmpFile));
    read32(bmpFile); //Commented out Serial to reduce variable size.
    (void)read32(bmpFile); // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile); // Start of image data
    //Serial.print("Image Offset: "); Serial.println(bmpImageoffset, DEC);
    // Read DIB header
    //Serial.print("Header size: "); Serial.println(read32(bmpFile));
    read32(bmpFile); //Commented out Serial to reduce variable size
    bmpWidth  = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    if (read16(bmpFile) == 1) { // # planes -- must be '1'
      bmpDepth = read16(bmpFile); // bits per pixel
      //Serial.print("Bit Depth: "); Serial.println(bmpDepth);
      if ((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed

        goodBmp = true; // Supported BMP format -- proceed!
        //Serial.print("Image size: ");
        //Serial.print(bmpWidth);
        //Serial.print('x');
        //Serial.println(bmpHeight);

        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * 3 + 3) & ~3;

        // If bmpHeight is negative, image is in top-down order.
        // This is not canon but has been observed in the wild.
        if (bmpHeight < 0) {
          bmpHeight = -bmpHeight;
          flip      = false;
        }

        // Crop area to be loaded
        w = bmpWidth;
        h = bmpHeight;
        if ((x + w - 1) >= tft.width())  w = tft.width()  - x;
        if ((y + h - 1) >= tft.height()) h = tft.height() - y;

        // Set TFT address window to clipped image bounds
        tft.setAddrWindow(x, y, x + w - 1, y + h - 1);

        for (row = 0; row < h; row++) { // For each scanline...

          // Seek to start of scan line.  It might seem labor-
          // intensive to be doing this on every line, but this
          // method covers a lot of gritty details like cropping
          // and scanline padding.  Also, the seek only takes
          // place if the file position actually needs to change
          // (avoids a lot of cluster math in SD library).
          if (flip) // Bitmap is stored bottom-to-top order (normal BMP)
            pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
          else     // Bitmap is stored top-to-bottom
            pos = bmpImageoffset + row * rowSize;
          if (bmpFile.position() != pos) { // Need seek?
            bmpFile.seek(pos);
            buffidx = sizeof(sdbuffer); // Force buffer reload
          }

          for (col = 0; col < w; col++) { // For each pixel...
            // Time to read more pixel data?
            if (buffidx >= sizeof(sdbuffer)) { // Indeed
              bmpFile.read(sdbuffer, sizeof(sdbuffer));
              buffidx = 0; // Set index to beginning
            }

            // Convert pixel from BMP to TFT format, push to display
            b = sdbuffer[buffidx++];
            g = sdbuffer[buffidx++];
            r = sdbuffer[buffidx++];
            tft.pushColor(tft.Color565(r, g, b));
          } // end pixel
        } // end scanline
        /* Commented out to save variable space.
          Serial.print("Loaded in ");
          Serial.print(millis() - startTime);
          Serial.println(" ms");
        */
      } // end goodBmp
    }
  }
  //If in Slideshow mode, add a "Play" indicator to the top/right
  if (mode == 1) {
    tft.fillTriangle(146, 2, 157, 13, 146, 24, ST7735_BLACK);
    tft.fillTriangle(147, 5, 155, 13, 147, 21, ST7735_WHITE);
  }
  if (lowbat) {
    //If our battery is low, show the low battery symbol
    drawLowBat(0, 0);
  }
  bmpFile.close();
  //if(!goodBmp) Serial.println("BMP format not recognized.");
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(File f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

long readVcc() {
  //Using method prodivded at http://provideyourown.com/2012/secret-arduino-voltmeter-measure-battery-voltage/
  //Read the 1.1V internal reference against AVcc
  // Set the reference to VCC and measure the difference between the 1.1v and reference.
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); //Allow Vref to settle.
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA, ADSC)); // measuring
  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
  uint8_t high = ADCH; // unlocks both
  long result = (high << 8) | low;
  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}
