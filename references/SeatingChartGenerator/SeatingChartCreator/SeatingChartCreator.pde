// required to load classes for ExcelClassFileReader
import org.apache.poi.hssf.usermodel.*;
import org.apache.poi.poifs.filesystem.*;

import seatingchart.*;
import controlP5.*;

import java.util.*;
import java.nio.file.*;
import java.io.*;
import javax.swing.JOptionPane;

/* Bugs (for all programs):
- ConcurrentModificationException issues - can sometimes occur while closing the window(?)
- Allows constraint distance to be greater than the size of the room
Duplicate students can appear?
Extra desks can appear
Changing allowed desks not always applied
*/



color backgroundColor = color(191, 255, 191);


void setup() {
  println("===============================");
  println("..:: Seating Chart Creator ::..");
  println("       Jacob van't Hoog");
  println("             2014");
  println("===============================");
  println();
  
  //768 - 64 = 704
  size(1024, 704);
  
  
  setupState();
  setupConstraints();
  setupGUI();
}


void draw() {
  background(backgroundColor);
  checkStateUpdates();
}


/*void test() {
  Student studentA = new Student("Student A", "", "", new GenderNone());
  Student studentB = new Student("Student B", "", "", new GenderNone());
  Student studentC = new Student("Student C", "", "", new GenderNone());
  studentA.addConstraint(new ProximityConstraint(2, false, studentB));
  studentC.addConstraint(new ProximityConstraint(3, false, studentA));
  studentC.addConstraint(new ProximityConstraint(1, false, studentB));
  for(int i = 0; i < 25; i++) {
    studentB.addConstraint(new ProximityConstraint(0, true, studentB));
  }
  currentStudentList.add(studentA);
  currentStudentList.add(studentB);
  currentStudentList.add(studentC);
  updateStudentList();
}*/