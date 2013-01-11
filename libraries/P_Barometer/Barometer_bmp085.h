/*  This is an BMP085 implementation pretty much "ripped" from aeroquad
    however i did re-write it from pure C to C++ class format
    
    TODO: This class needs quite a bit of polishing and proper documentation
*/

// BMP085 Registers
#define BMP085_ADDRESS 0x77

// Operating Modes
#define BMP085_ULTRALOWPOWER 0
#define BMP085_STANDARD      1
#define BMP085_HIGHRES       2
#define BMP085_ULTRAHIGHRES  3

#define BMP085_CAL_AC1           0xAA  // R   Calibration data (16 bits)
#define BMP085_CAL_AC2           0xAC  // R   Calibration data (16 bits)
#define BMP085_CAL_AC3           0xAE  // R   Calibration data (16 bits)    
#define BMP085_CAL_AC4           0xB0  // R   Calibration data (16 bits)
#define BMP085_CAL_AC5           0xB2  // R   Calibration data (16 bits)
#define BMP085_CAL_AC6           0xB4  // R   Calibration data (16 bits)
#define BMP085_CAL_B1            0xB6  // R   Calibration data (16 bits)
#define BMP085_CAL_B2            0xB8  // R   Calibration data (16 bits)
#define BMP085_CAL_MB            0xBA  // R   Calibration data (16 bits)
#define BMP085_CAL_MC            0xBC  // R   Calibration data (16 bits)
#define BMP085_CAL_MD            0xBE  // R   Calibration data (16 bits)

#define BMP085_CONTROL           0xF4 
#define BMP085_TEMPDATA          0xF6
#define BMP085_PRESSUREDATA      0xF6

#define BMP085_READTEMPCMD       0x2E
#define BMP085_READPRESSURECMD   0x34

double baroRawAltitude = 0.0;
double baroGroundAltitude = 0.0; 
double baroAltitude = 0.0;
double baroAltitudeRunning = 0.0;

double baroAltitudeToHoldTarget = 0.0;
int16_t baroAltitudeHoldThrottle = 1000;

class BMP085 {
    public:
        // constructor
        BMP085() {
        };
    
        void initialize() {
            baroAltitude = 0.0;
            
            overSamplingSetting = BMP085_STANDARD;
            pressureFactor = 1/5.255;
            baroSmoothFactor = 0.02;
            
            // Read Calibration Data
            ac1 = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_AC1);
            ac2 = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_AC2);
            ac3 = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_AC3);
            ac4 = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_AC4);
            ac5 = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_AC5);
            ac6 = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_AC6);

            b1 = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_B1);
            b2 = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_B2);

            mb = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_MB);
            mc = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_MC);
            md = sensors.i2c_read16(BMP085_ADDRESS, BMP085_CAL_MD); 
            
            requestRawTemperature(); // setup up next measure() for temperature
            isReadPressure = false;
            
            pressureCount = 0;
            measureBaro();
            
            delay(5); // delay for temperature
            measureBaro();
            
            delay(10); // delay for pressure
            measureGroundBaro();
            
            // check if measured ground altitude is valid
            while (abs(baroRawAltitude - baroGroundAltitude) > 10) {
                delay(26);
                measureGroundBaro();
            }
            
            baroAltitude = baroGroundAltitude;
        };
        
        void requestRawPressure() {
            sensors.i2c_write8(BMP085_ADDRESS, BMP085_CONTROL, BMP085_READPRESSURECMD + (overSamplingSetting << 6));
        };
        
        long readRawPressure() {            
            Wire.beginTransmission(BMP085_ADDRESS);
            Wire.write(BMP085_PRESSUREDATA);
            Wire.endTransmission();
            
            Wire.requestFrom(BMP085_ADDRESS, 3);
            
            return (((unsigned long)Wire.read() << 16) | ((unsigned long)Wire.read() << 8) | ((unsigned long)Wire.read())) >> (8 - overSamplingSetting);
        };
        
        void requestRawTemperature() {
            sensors.i2c_write8(BMP085_ADDRESS, BMP085_CONTROL, BMP085_READTEMPCMD);
        };
        
        unsigned int readRawTemperature() {
            uint16_t data;
            
            Wire.beginTransmission(BMP085_ADDRESS);
            Wire.write(BMP085_TEMPDATA);
            Wire.endTransmission();
            
            Wire.requestFrom(BMP085_ADDRESS, 2);
            
            data = (Wire.read() << 8) | Wire.read();
            
            return data;
        };
        
        void measureBaro() {
            measureBaroSum();
            evaluateBaroAltitude();
        };
        
        void measureBaroSum() {
            // switch between pressure and temperature measurements
            // each loop, since it is slow to measure pressure
            if (isReadPressure) {
                rawPressureSum += readRawPressure();
                rawPressureSumCount++;
                
                if (pressureCount == 4) {
                    requestRawTemperature();
                    pressureCount = 0;
                    isReadPressure = false;
                } 
                else {
                    requestRawPressure();
                }
                pressureCount++;
            } else { // select must equal TEMPERATURE
                rawTemperature = (long)readRawTemperature();
                requestRawPressure();
                isReadPressure = true;
            }
        };
        
        void measureGroundBaro() {
            // measure initial ground pressure (multiple samples)
            float altSum = 0.0;
            for (int i = 0; i < 25; i++) {
                measureBaro();
                altSum += baroRawAltitude;
                delay(12);
            }
            
            baroGroundAltitude = altSum / 25;
        };
        
        void evaluateBaroAltitude() {
            long x1, x2, x3, b3, b5, b6, p;
            unsigned long b4, b7;
            int32_t tmp;

            //calculate true temperature
            x1 = ((long)rawTemperature - ac6) * ac5 >> 15;
            x2 = ((long) mc << 11) / (x1 + md);
            b5 = x1 + x2;

            if (rawPressureSumCount == 0) { // it may occur at init time that no pressure has been read yet!
                return;
            }
            
            rawPressure = rawPressureSum / rawPressureSumCount;
            rawPressureSum = 0.0;
            rawPressureSumCount = 0;

            //calculate true pressure
            b6 = b5 - 4000;
            x1 = (b2 * (b6 * b6 >> 12)) >> 11; 
            x2 = ac2 * b6 >> 11;
            x3 = x1 + x2;

            // Real Bosch formula - b3 = ((((int32_t)ac1 * 4 + x3) << overSamplingSetting) + 2) >> 2;
            // The version below is the same, but takes less program space
            tmp = ac1;
            tmp = (tmp * 4 + x3) << overSamplingSetting;
            b3 = (tmp + 2) >> 2;

            x1 = ac3 * b6 >> 13;
            x2 = (b1 * (b6 * b6 >> 12)) >> 16;
            x3 = ((x1 + x2) + 2) >> 2;
            b4 = (ac4 * (uint32_t) (x3 + 32768)) >> 15;
            b7 = ((uint32_t) rawPressure - b3) * (50000 >> overSamplingSetting);
            p = b7 < 0x80000000 ? (b7 << 1) / b4 : (b7 / b4) << 1;

            x1 = (p >> 8) * (p >> 8);
            x1 = (x1 * 3038) >> 16;
            x2 = (-7357 * p) >> 16;
            pressure = (p + ((x1 + x2 + 3791) >> 4));

            baroRawAltitude = 44330 * (1 - pow(pressure / 101325.0, pressureFactor)); // returns absolute baroAltitude in meters 
            baroAltitude = filterSmooth(baroRawAltitude, baroAltitude, baroSmoothFactor);
        };
        
        void getBaroAltitude() {
            baroAltitudeRunning = baroAltitude - baroGroundAltitude;
        };
    private:
        int16_t ac1, ac2, ac3, b1, b2, mb, mc, md;
        uint16_t ac4, ac5, ac6;

        uint8_t overSamplingSetting;
        bool isReadPressure;
        
        float rawPressureSum;
        uint8_t rawPressureSumCount;
        
        long pressure, rawPressure, rawTemperature;
        uint8_t pressureCount;
        
        float pressureFactor;
        float baroSmoothFactor;
};

// Create Baro object
BMP085 baro;

void SensorArray::initializeBaro() {
    baro.initialize();
}

void SensorArray::readBaroSum() {
    baro.measureBaroSum();
}

void SensorArray::evaluateBaroAltitude() {
    baro.evaluateBaroAltitude();
    baro.getBaroAltitude();
}