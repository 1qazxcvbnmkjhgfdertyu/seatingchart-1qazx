import seatingchart.*;

import controlP5.*;
import processing.pdf.*;

import java.util.*;
import java.nio.file.*;
import javax.swing.JOptionPane;

PGraphicsPDF pdf;

int deskSize = 64;
final int deskStartX = 8;
final int deskStartY = 64;
final int deskXMargin = 4;
final int deskYMargin = 4;
PFont font;
final String fontName = "ArialMT-16";
float fontSize = 16;
PFont titleFont;
final String titleFontName = "defaultFontLarge";
final int titleFontSize = 48;
final int textOffsetX = 8;
final int textOffsetY = 16;

String saveFileName;

boolean fileLoaded = false;
boolean recording = false;
boolean firstDraw = true;
boolean drawDesks = false;

SeatingArrangement arrangement = null;

String titleText = "";

Path chartFile;


void setup() {
  size(1024, 704);
  
  titleFont = createFont(titleFontName, titleFontSize);
  
  selectInput("Choose a seating chart file", "loadFileChosen");
}

void loadFileChosen(File f) {
  if(f == null) {
    exit();
    return;
  }
  
  chartFile = f.toPath();
  
  selectOutput("Choose a file to save to", "saveFileChosen");
}


void saveFileChosen(File f) {
  if(f == null) {
    exit();
    return;
  }
  
  saveFileName = f.toString();
  if(!saveFileName.endsWith(".pdf"))
    saveFileName = saveFileName + ".pdf";
  
  fileLoaded = true;
  
  drawDesks();
}

void drawDesks() {
  drawDesks = true;
}

void draw() {
  if(firstDraw) {
    firstDraw = false;
    background(255);
    textFont(titleFont, titleFontSize);
    fill(color(0));
    text("Choose a chart file...", 4, 4, width, 64);
    return;
  }
  
  if(chartFile != null && !fileLoaded) {
    background(255);
    textFont(titleFont, titleFontSize);
    fill(color(0));
    text("Choose file to save to...", 4, 4, width, 64);
    return;
  }
  
  if(drawDesks) {
    if(recording) {
      println("Start recording");
      pdf = (PGraphicsPDF) createGraphics(width, height, PDF, saveFileName);
      beginRecord(pdf);
    } else {
      background(255);
    }
    
    rectMode(CORNER);
    
    stroke(color(0));
    font = createFont(fontName, fontSize);
    textFont(font, fontSize);
    
    try {
      if(arrangement == null) {
        DataFile file = new DataFileVersion2(this, chartFile);
        ClassGroup classGroup = file.readStudents();
        arrangement = file.readDesks();
        file.addStudentsToDesks(classGroup, arrangement, classGroup.getStudents());
      }
    } catch (Exception e) {
      JOptionPane.showMessageDialog(null,
      "Error reading file: " + e.toString(),
      "Error",
      JOptionPane.ERROR_MESSAGE);
      exit();
      return;
    }
    
    calculateDeskSize(arrangement.getXSize(), arrangement.getYSize());
    
    for(Desk d : arrangement.getDesks()) {
      fill(color(255));
      rect(d.getX() * deskSize + deskStartX, d.getY() * deskSize + deskStartY,
        deskSize, deskSize);
      
      Student s = d.getStudent();
      String studentName = "";
      if(s != null) {
        studentName = s.getName(NameStyle.FIRST_LAST_INITIAL);
      }
      
      fill(color(0));
      
      text(studentName.replace(' ', '\n'),
        d.getX() * deskSize + deskStartX + textOffsetX,
        d.getY() * deskSize + deskStartY + textOffsetY,
        deskSize, deskSize);
      
    }
    
    
    
    textFont(titleFont, titleFontSize);
    fill(0);
    text(titleText, 6, 4, width - 8, 64);
    
    if(recording) {
      println("Done recording");
      endRecord();
      
      doneMessage();
    }
    
    recording = false;
    
    if(cp5 == null) {
      setupGUI();
    }
    
    drawDesks = false;
  }
}


void doneMessage() {
  stroke(255);
  fill(255);
  rect(0, 0, width, deskStartY - 2);
  
  textFont(titleFont, titleFontSize);
  fill(color(0, 255, 0));
  text("File saved, ready for printing.", 6, 4, width - 8, 64);
}


void keyPressed() {
  drawDesks();
}


void calculateDeskSize(int xLen, int yLen) {
  int screenXLen = width - deskStartX - deskXMargin;
  int screenYLen = height - deskStartY - deskYMargin;
  
  int size1 = screenXLen / xLen;
  int size2 = screenYLen / yLen;
  
  if(size1 > size2) {
    deskSize = size2;
  } else {
    deskSize = size1;
  }
}