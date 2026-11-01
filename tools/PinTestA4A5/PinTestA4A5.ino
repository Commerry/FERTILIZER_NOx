// PinTestA4A5 -- กระพริบ A4 กับ A5 สลับกัน ทุก 1 วิ (เทสขาโดด ๆ)
//   วัด A4<->GND และ A5<->GND: ต้องสวิง 0<->5V ตามที่พิมพ์
//   ถ้า A4 สวิงแต่ A5 ไม่ = ขา A5 บนบอร์ดนี้เสีย
//   Serial 9600
void setup(){
  pinMode(A4, OUTPUT);
  pinMode(A5, OUTPUT);
  digitalWrite(A4, LOW);
  digitalWrite(A5, LOW);
  Serial.begin(9600);
  Serial.println();
  Serial.println(F("=== PIN TEST A4 / A5 (blink 1Hz) ==="));
}
void loop(){
  digitalWrite(A4, HIGH); digitalWrite(A5, LOW);
  Serial.println(F("A4=HIGH(5V)   A5=LOW(0V)"));
  delay(1500);
  digitalWrite(A4, LOW);  digitalWrite(A5, HIGH);
  Serial.println(F("A4=LOW(0V)    A5=HIGH(5V)"));
  delay(1500);
}
