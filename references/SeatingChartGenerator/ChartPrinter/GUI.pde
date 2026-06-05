ControlP5 cp5;

Textfield title;
Slider fontSizeSlider;

void setupGUI() {
  println("Setup GUI");
  
  cp5 = new ControlP5(this);
  
  CColor c = ControlP5.THEME_CP5BLUE;
  c.setForeground(color(128));
  c.setValueLabel(color(0));
  c.setCaptionLabel(color(0));
  c.setBackground(color(192));
  
  cp5.setColor(c);
  
  title = cp5.addTextfield("Title")
    .setPosition(4, 4)
    .setSize(width - 8, 56)
    .setLabel("")
    .setText("Title")
    .setFont(titleFont)
    .setColorCursor(color(0))
    .setAutoClear(true)
    .setFocus(true)
    ;
  
  fontSizeSlider = cp5.addSlider("Font Size")
    .setPosition(4, height - 32)
    .setWidth(width - 16)
    .setRange(8, 24)
    .setLabel("")
    .setValue(fontSize)
    ;
}

void controlEvent(ControlEvent event) {
  println("ControlEvent");
  if(event.isController()) {
    if(event.getController() == title) {
      cp5.setAutoDraw(false);
      
      titleText = event.getStringValue();
      
      recording = true;
      drawDesks();
    }
    
    if(event.getController() == fontSizeSlider) {
      fontSize = fontSizeSlider.getValue();
      drawDesks();
    }
  }
}