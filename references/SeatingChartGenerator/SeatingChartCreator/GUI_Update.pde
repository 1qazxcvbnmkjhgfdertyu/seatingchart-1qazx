import java.util.Map.Entry; //Quick fix to get iterating to work



int setupDeskButtons = -1;
final int setupDeskButtonsCount = 1;
int setupStudentList = -1;
final int setupStudentListCount = 2;
int setupConstraintList = -1;
final int setupConstraintListCount = 2;
int setupConstraintGUI = -1;
final int setupConstraintGUICount = 1;
int setupAllowedDesksGUI = -1;
final int setupAllowedDesksGUICount = 2;
void prepareSetupAllowedDesksGUI() { setupAllowedDesksGUI = 0; }
int setupResetGUI = -1;
final int setupResetGUICount = 2;
void prepareSetupResetGUI() { setupResetGUI = 0; }
int setupErrorMessage = -1;
final int setupErrorMessageCount = 2;
String error;


void checkStateUpdates() {
//  noLoop();
//  
//  Thread t = new Thread(new Runnable() {
//    public void run() {
//      try {
//        Thread.sleep(100);
//      } catch(Exception e) {
//        println("Exception when sleeping: " + e.toString());
//      }
      if(setupDeskButtons >= 0) {
        if(++setupDeskButtons == setupDeskButtonsCount) {
          setupDeskButtons();
          setupDeskButtons = -1;
        }
      }
      
      if(setupStudentList >= 0) {
        if(++setupStudentList == setupStudentListCount) {
          setupStudentList = -1;
          setupStudentList();
        }
      }
      
      if(setupConstraintList >= 0) {
        if(++setupConstraintList == setupConstraintListCount) {
          setupConstraintList = -1;
          setupConstraintList();
        }
      }
      
      if(setupConstraintGUI >= 0) {
        if(++setupConstraintGUI == setupConstraintGUICount) {
          setupConstraintGUI = -1;
          setupConstraintGUI();
        }
      }
      
      if(setupAllowedDesksGUI >= 0) {
        if(++setupAllowedDesksGUI == setupAllowedDesksGUICount) {
          setupAllowedDesksGUI = -1;
          setupAllowedDesksGUI();
        }
      }
      
      if(setupResetGUI >= 0) {
        if(++setupResetGUI == setupResetGUICount) {
          setupResetGUI = -1;
          setupResetGUI();
        }
      }
      
      if(setupErrorMessage >= 0) {
        if(++setupErrorMessage == setupErrorMessageCount) {
          setupErrorMessage = -1;
          setupErrorMessage();
        }
      }
      
//      loop();
//    }
//  });
//  
//  t.start();
}


void setupDeskButtons() {
  println("Setup desk buttons");
  
  if(currentDeskList == null)
    return;
  
  if(!deskButtons.isEmpty()) {
    for(Entry<Button, Desk> entry : deskButtons.entrySet()) { //Uses import above
      Button b = entry.getKey();
      b.remove();
    }
    
    deskButtons.clear();
  }
  
  if(!deskClearButtons.isEmpty()) {
    for(Entry<Button, Desk> entry : deskClearButtons.entrySet()) { //Uses import above
      Button b = entry.getKey();
      b.remove();
    }
    
    deskClearButtons.clear();
  }
  
  
  int i = 0;
  for(Desk d : currentDeskList) {
    String studentName = "";
    if(d.getStudent() != null) {
      studentName = d.getStudent().getName(NameStyle.FIRST_LAST).replace(' ', '\n');
    }
    
    Button b = cp5.addButton("Desk " + i)
      .setPosition(deskButtonX + d.getX()*deskButtonSpacing, deskButtonY + d.getY()*deskButtonSpacing)
      .setSize(deskButtonSize, deskButtonSize)
      .setLabel(studentName)
      .setValue(i)
      ;
    deskButtons.put(b, d);
    addEvent(b, buttonAction);
    
    b.getCaptionLabel().getStyle().setMargin(-(deskButtonSize / 4),0,0,0); 
    
    
    Button cb = cp5.addButton("DeskClear" + i)
      .setPosition(deskButtonX + d.getX()*deskButtonSpacing + (deskButtonSize - deskClearButtonSize), deskButtonY + d.getY()*deskButtonSpacing)
      .setSize(deskClearButtonSize, deskClearButtonSize)
      .setLabel(deskClearButtonLabel)
      .setValue(i)
      ;
    deskClearButtons.put(cb, d);
    addEvent(cb, clearButtonAction);
    
    
    i++;
  }
}

void setupStudentList() {
  
  println("Setup student list");
  
  lstStudents.clear();
  int i = 0;
  for(Student s : currentStudentList) {
    String studentString = s.getName(NameStyle.FIRST_LAST) + "    ";
    if(s.getConstraints().size() > 0)
      studentString = studentString + "(" + s.getConstraints().size() + " const.)    ";
    if(currentDeskList != null && currentSeatingChart != null) {
      if(s.getAllowedDesks(currentSeatingChart).size() < currentDeskList.size()) {
        studentString = studentString + "Ltd. Desks  ";
      }
    }
    lstStudents.addItem(studentString, i++);
  }
  
  lstStudents.setLabel("Students (" + currentStudentList.size() + ")");
  
  updateSelectedStudent();
}


void setupConstraintList() {
  
  println("Setup constraint list");
  
  lstConstraints.clear();
  if(selectedStudent != null) {
    int i = 0;
    for(SeatingConstraint c : selectedStudent.getConstraints()) {
      lstConstraints.addItem(c.toString(), i++);
    }
    
    lstConstraints.setLabel("Constraints for " + selectedStudent.getName(NameStyle.FIRST));
  } else {
    lstConstraints.setLabel("Constraints");
  }
  
  updateSelectedConstraint();
}


void setupConstraintGUI() {
  if(currentPropertyControls != null) {
    for(ControllerInterface c : currentPropertyControls) {
      c.remove();
    }
  }
  
  if(selectedConstraint != null) {
    currentPropertyControls = selectedConstraint.createGUI(cp5, grpConstraintProperties);
  }
}


void setupAllowedDesksGUI() {
  if(currentSeatingChart == null || selectedStudent == null)
    return;
  resetGUI();
  final int xLen = currentSeatingChart.getXSize();
  final int yLen = currentSeatingChart.getYSize();
  
  mtxDesks = cp5.addMatrix("Allowed Desks")
    .setLabel("Allowed desks for " + selectedStudent.getName(NameStyle.FIRST_LAST))
    .setPosition(deskButtonX, deskButtonY)
    .setSize(deskButtonSpacing * xLen, deskButtonSpacing * yLen)
    .setGrid(xLen, yLen)
    .setMode(ControlP5.MULTIPLES)
    //.set(0, 0, true)
    .setInterval(0)
    .setColorActive(allowedDeskColor)
    .setColorBackground(bannedDeskColor)
    .setColorCaptionLabel(labelColor)
    .pause()
    ;
  
  cvsBlankDesks = new DrawCanvas();
  cvsBlankDesks.post();
  cp5.addCanvas(cvsBlankDesks);
  
  for(int y = 0; y < yLen; y++) {
    for(int x = 0; x < xLen; x++) {
      Desk d = currentSeatingChart.getDesk(x, y);
      if(d != null) {
        
      } else {
        println("Rectangle added");
        cvsBlankDesks.add(new Rectangle(deskButtonX + x*deskButtonSpacing, deskButtonY + y*deskButtonSpacing,
          deskButtonSize, deskButtonSize,
          matrixBlankColor, matrixBlankColor));
      }
    }
  }
  
  for(Desk d : selectedStudent.getAllowedDesks(currentSeatingChart)) {
    mtxDesks.set(d.getX(), d.getY(), true);
  }
  
  
  btnExitAllowedDesks = cp5.addButton("Done")
    .setPosition(width - 64, 0)
    .setSize(64, 24)
    ;
  addEvent(btnExitAllowedDesks, new CP5EventAction() {
    public void run(ControlEvent e) {
      //Save allowed desks
      for(int y = 0; y < yLen; y++) {
        for(int x = 0; x < xLen; x++) {
          Desk d = currentSeatingChart.getDesk(x, y);
          if(d != null) {
            if(mtxDesks.get(x, y)) {
              selectedStudent.allowDesk(d);
            } else {
              selectedStudent.banDesk(d);
            }
          }
        }
      }
      
      prepareSetupResetGUI();
    }
  });
}


void setupResetGUI() {
  resetGUI();
  setupGUI();
}

void setupErrorMessage() {
  lblError.setText(error);
  grpError.setVisible(true);
  grpError.bringToFront();
}
