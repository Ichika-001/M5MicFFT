#include <Arduino.h>
#include <M5Unified.h>
#include "arduinoFFT.h"
#include <ESP32Servo.h>  // ESP32用のサーボライブラリ。ESP32とはM5stackCore2に搭載されている開発ボード。ESP32では通常のServo.hライブラリがそのまま作動しないためこのライブラリを用いる。


#define MIC 33 // for Core2's PortA (Pin1)
#define SERVO1_PIN 26 // for Core2's PortB (Pin2)-右車輪
#define SERVO2_PIN 14 // for Core2's PortC (Pin2)-左車輪

#define SAMPLING_FREQUENCY 5000 //5kHz...8音ハンドベルに合わせた音域
const uint16_t FFTsamples = 256;  // サンプル数は2のべき乗

double vReal[FFTsamples]; // サンプリングデータ,フーリエ変換後の複素数の実部,その複素数を実数に変換した値が格納される
double vImag[FFTsamples]; //フーリエ変換後の複素数の虚部が格納される
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFTsamples, SAMPLING_FREQUENCY);  // FFTオブジェクトを作る

unsigned int sampling_period_us;

Servo rightWheel;  // 右車輪用のサーボオブジェクト
Servo leftWheel;   // 左車輪用のサーボオブジェクト
unsigned long startTime;  // サーボの動作開始時間を記録

bool servoRunning = false; // サーボが動作中かどうかを判定する

// 音の周波数範囲 (Hz)
struct Note {
    double frequencyMin;
    double frequencyMax;
};

Note notes[] = {
    {1050, 1060}, // ド=0
    {1186, 1196}, // レ=1
    {1323, 1333}, // ミ=2
    {1401, 1411}, // ファ=3
    {1577, 1587},// ソ=4
    {1791, 1801}// ラ=5
};

//チュートリアル
/*
const int kirakiraboshi[] = {
  0, 1, 2, 4, 3, 88, 5//ドレミソファーラ
  };
int kirakiraboshiIndex = 0;
*/

// きらきら星

const int kirakiraboshi[] = {
  0, 0, 4, 4, 5, 5, 4, 88,//88を休符とする
  3, 3, 2, 2, 1, 1, 0, 88,
  4, 4, 3, 3, 2, 2, 1, 88,
  4, 4, 3, 3, 2, 2, 1, 88,
  0, 0, 4, 4, 5, 5, 4, 88,
  3, 3, 2, 2, 1, 1, 0, 88};
int kirakiraboshiIndex = 0;


void sample(int nsamples) {
	for (int i = 0; i < nsamples; i++) {
		unsigned long t = micros();
		vReal[i] = (double)analogRead(MIC) / 4095.0 * 3.6 + 0.1132; // ESP32のADCの特性を補正
		vImag[i] = 0;
		while ((micros() - t) < sampling_period_us) ;
  }
}

int X0 = 30;
int Y0 = 20;
int _height = 240 - Y0;
int _width = 320;
float dmax = 5.0;

void drawChart(int nsamples) {
	int band_width = floor(_width / nsamples);
	int band_pad = band_width - 1;

	for (int band = 0; band < nsamples; band++) {
		int hpos = band * band_width + X0;
		float d = vReal[band];
		if (d > dmax) d = dmax;
    int h = (int)((d / dmax) * (_height));
		M5.Lcd.fillRect(hpos, _height - h, band_pad, h, WHITE);
	//if ((band % (nsamples / 4)) == 0) {
	if ((band % (nsamples / 5)) == 0) {
			M5.Lcd.setCursor(hpos, _height + Y0 - 10);
			M5.Lcd.printf("%.1fkHz", ((band * 1.0 * SAMPLING_FREQUENCY) / FFTsamples / 1000));
		}
	}
}

void setup() {
	M5.begin();
	pinMode(MIC, INPUT);//マイクから入力
  pinMode(27,OUTPUT);

	sampling_period_us = round(1000000 * (1.0 / SAMPLING_FREQUENCY));
	M5.Lcd.printf("%d", SAMPLING_FREQUENCY);

    // サーボの初期化
    rightWheel.attach(SERVO1_PIN);
    leftWheel.attach(SERVO2_PIN);

  startTime = millis();  // 動作開始時刻を記録
}

void DCRemoval(double *vData, uint16_t samples) {
	double mean = 0;

	for (uint16_t i = 1; i < samples; i++) {
		mean += vData[i];
	}
	mean /= samples;
	for (uint16_t i = 1; i < samples; i++) {
  	vData[i] -= mean;
	}
}

int detectNote() {
  digitalWrite(27,1);
    sample(FFTsamples);
  digitalWrite(27,0); 
    DCRemoval(vReal, FFTsamples);
    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);// 窓関数
    FFT.compute(FFT_FORWARD);// FFT処理(複素数で計算)
    FFT.complexToMagnitude();// 複素数を実数に変換

    double maxValue = 0;
    uint16_t maxIndex = 0;
    for (uint16_t i = 1; i < (FFTsamples / 2); i++) {
        if (vReal[i] > maxValue) {
            maxValue = vReal[i];
            maxIndex = i;
        }
    }

    double dominantFrequency = (maxIndex * 1.0 * SAMPLING_FREQUENCY) / FFTsamples;

    // 各音の範囲を確認し、検出した音が正しいかどうかを返す
    for (int i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        if (dominantFrequency >= notes[i].frequencyMin && dominantFrequency <= notes[i].frequencyMax) {
            return i;
        }
    }
    return -1; // 該当音なし
}


void loop() {
  if (!servoRunning) {// サーボが停止中(動作中ではない時)のみ音を検知するという意味。!servoRunningはservoRunning==falseと同じ。
    int detectedNote = detectNote();
      // 88(休符)の場合は問答無用で1秒進む
    if (kirakiraboshi[kirakiraboshiIndex] == 88) {
        startTime = millis();
        servoRunning = true;
        kirakiraboshiIndex = (kirakiraboshiIndex + 1) % (sizeof(kirakiraboshi) / sizeof(kirakiraboshi[0]));
    }else if (detectedNote == kirakiraboshi[kirakiraboshiIndex]) {
            startTime = millis();
            servoRunning = true;
            kirakiraboshiIndex = (kirakiraboshiIndex + 1) % (sizeof(kirakiraboshi) / sizeof(kirakiraboshi[0]));
        }
 
    M5.Lcd.fillScreen(BLACK);
    drawChart(FFTsamples / 2);

    double maxValue = 0;
    uint16_t maxIndex = 0;
    //uint16_t minIndex = (500 * FFTsamples) / SAMPLING_FREQUENCY; // 500Hz以下の成分を無視するためのインデックス
    //uint16_t minIndex = (1000 * FFTsamples) / SAMPLING_FREQUENCY; // 1000Hz以下の成分を無視するためのインデックス
    for (uint16_t i = 1; i < (FFTsamples / 2); i++) { //i=1から始めることでDC成分(直流成分=0Hzの周波数)を無視。
      //for (uint16_t i = minIndex; i < (FFTsamples / 2); i++) { //i=minIndexから始めることで雑音を無視。
	    if (vReal[i] > maxValue) { //ここのvRealはフーリエ変換によって導き出された複素数を実数に変換したもの=各周波数成分の振幅データ(の[配列])
        maxValue = vReal[i]; //maxValueには↑の中で最も大きな値が格納される。それと対応する周波数が最も振幅(音量)が大きな周波数
        maxIndex = i;
		  }
    }

    double dominantFrequency = (maxIndex * 1.0 * SAMPLING_FREQUENCY) / FFTsamples; 

    // maxValue と maxIndex を表示する
    M5.Lcd.setCursor(0, 0); // 表示位置を設定
    M5.Lcd.printf("Max Value: %.2f\n", maxValue);
    M5.Lcd.printf("Max Index: %d\n", maxIndex);
    M5.Lcd.printf("Dominant Frequency: %.2f Hz\n", dominantFrequency);

    // シリアルモニターにも表示
    Serial.printf("Max Value: %.2f\n", maxValue);
    Serial.printf("Max Index: %d\n", maxIndex);
    Serial.printf("Dominant Frequency: %.2f Hz\n", dominantFrequency);
  }

  // サーボの制御
  unsigned long currentTime = millis();
  if (servoRunning) {//サーボが停止中⋀2120~2024Hzの音を検知したとき、servoRunning==trueとなり以下の動作が行われる。
    if (currentTime - startTime < 1000) {//ここで設定した秒数前進する。
      rightWheel.write(81.5);  // 右車輪は数字が0~90の間で前進し、小さくなるほど早くなる。
      leftWheel.write(120); // 左車輪は数字が90~180の間で前進し、大きくなるほど早くなる。
      //右車輪のほうが速いので左斜め前にずれる。値で調整する。

    } else {
      // 停止指示
      rightWheel.write(90);   //90で停止。
      leftWheel.write(90);
      servoRunning = false;  // サーボ動作完了フラグをリセット
      }
  }

}