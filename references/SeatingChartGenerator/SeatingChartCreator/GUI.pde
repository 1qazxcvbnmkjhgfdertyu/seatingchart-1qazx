ControlP5 cp5;


final color groupBackgroundColor = color(127);
final color listSelectedColor = color(63, 191, 63);
final color matrixBlankColor = color(127);
final color allowedDeskColor = color(0, 255, 0);
final color bannedDeskColor = color(255, 0, 0);
final color labelColor = color(0);


final int deskButtonX = 68;
final int deskButtonY = 0;
int deskButtonSize = 54;
final int deskClearButtonSize = 12;
int deskButtonSpacing = 56;
final String deskClearButtonLabel = "X";


final int fileAccordionWidth = 66;
final int studentGroupHeight = 256;
final int studentAccordionWidth = 192;


Map<Button, Desk> deskButtons;
Map<Button, Desk> deskClearButtons;

CP5EventAction buttonAction;
CP5EventAction clearButtonAction;


Button btnClearAll;

Matrix mtxDesks;
DrawCanvas cvsBlankDesks;
Button btnExitAllowedDesks;


Button btnOpenFile;
Accordion acdFiles;
Group grpClassFile;
Button btnSaveConstraintFile;
Button btnClearConstraints;
Group grpSeatingFile;
Button btnSaveSeatingFile;
Button btnPrintSeatingFile;


Accordion acdStudents;
Group grpRandomize;
Button btnRandomize;
RadioButton rdoFillMode;
Group grpStudents;
Textlabel lblSelectedStudent;
ListBox lstStudents;
Group grpConstraints;
Button btnAllowedDesks;
DropdownList ddlConstraintMenu;
Button btnDeleteConstraint;
ListBox lstConstraints;
Group grpConstraintProperties;


Group grpError;
Textlabel lblError;
Button btnErrorOk;


void resetGUI() {
  println("Reset GUI...");
  
  if(cp5 != null) {
    cp5.removeCanvas(cvsBlankDesks);
    for(ControllerInterface c : cp5.getAll()) {
      c.remove();
    }
  }
  
  cp5 = new ControlP5(this);
  //cp5.setMoveable(true);
  //PFont font = loadFont("DroidSans-8.vlw");
  //ControlFont cFont = new ControlFont(font, 8);
  //cp5.setFont(cFont);
  //cp5.setColor(ControlP5.WHITE);
  
  setupEvents();
  
  btnClearAll = null;
  
  mtxDesks = null;
  cvsBlankDesks = null;
  btnExitAllowedDesks = null;
  
  
  btnOpenFile = null;
  acdFiles = null;
  grpClassFile = null;
  btnSaveConstraintFile = null;
  grpSeatingFile = null;
  btnSaveSeatingFile = null;
  btnPrintSeatingFile = null;
  
  
  acdStudents = null;
  grpRandomize = null;
  btnRandomize = null;
  rdoFillMode = null;
  grpStudents = null;
  lblSelectedStudent = null;
  lstStudents = null;
  grpConstraints = null;
  btnAllowedDesks = null;
  ddlConstraintMenu = null;
  btnDeleteConstraint = null;
  lstConstraints = null;
  grpConstraintProperties = null;
  
  
  grpError = null;
  lblError = null;
  btnErrorOk = null;
}


void setupGUI() {
  println("Setup GUI...");
  resetGUI();
  
  cp5.addTextlabel("Info")
    .setText("Jacob van't Hoog, 2015")
    .setPosition(0, height - 16)
    .setWidth(width)
    .setColor(color(0))
    ;
  
  
  setupFileAccordion();
  setupStudentAccordion();
  
  deskButtons = new HashMap<Button, Desk>();
  deskClearButtons = new HashMap<Button, Desk>();
  
  buttonAction = new CP5EventAction() {
    public void run(ControlEvent c) {
      int i = (int)c.getValue();
      println("Desk button clicked: " + i);
      
      Button b = (Button)c.getController();
      Desk d = deskButtons.get(b);
      
      if(selectedStudent != null) {
        if(d.getStudent() != null) {
          currentStudentList.add(d.getStudent());
        }
        d.setStudent(selectedStudent);
        currentStudentList.remove(selectedStudent);
        
        selectedStudent = null;
        updateSelectedStudent();
        
        updateStudentList();
        updateDesks();
      }
    }
  };
  
  clearButtonAction = new CP5EventAction() {
    public void run(ControlEvent c) {
      int i = (int)c.getValue();
      println("Desk clear button clicked: " + i);
      
      Button b = (Button)c.getController();
      Desk d = deskClearButtons.get(b);
      
      if(d.getStudent() != null) {
        currentStudentList.add(d.getStudent());
        d.setStudent(null);
        
        updateStudentList();
        updateDesks();
      }
    }
  };
  
  
  btnClearAll = cp5.addButton("Clear All Seats")
    .setPosition(width - studentAccordionWidth, 0)
    .setSize(80, 23)
    ;
  addEvent(btnClearAll, new CP5EventAction() {
    public void run(ControlEvent c) {
      clearAllDesks();
    }
  });
  
  
  setupErrorGroup();
  
  
  updateDesks();
  updateStudentList();
  
  println();
}

void setupFileAccordion() {
  btnOpenFile = cp5.addButton("GenericOpen") 
    .setLabel("Load File")
    .setPosition(0, 0)
    .setSize(fileAccordionWidth, 24)
    ;
  addEvent(btnOpenFile, new CP5EventAction() {
    public void run(ControlEvent e) {
      selectInput("Choose a file to open:", "genericFileOpen");
    }
  });
  
  grpClassFile = cp5.addGroup("ConstraintsGroup")
    .setLabel("Constraints")
    .setBackgroundColor(groupBackgroundColor)
    .setBackgroundHeight(28)
    ;
  
  btnSaveConstraintFile = cp5.addButton("SaveConstraints")
    .setLabel("Save")
    .setPosition(2, 2)
    .setSize(30, 24)
    .moveTo(grpClassFile)
    ;
  addEvent(btnSaveConstraintFile, new CP5EventAction() {
    public void run(ControlEvent e) {
      if(currentClass == null || currentClassFile == null || currentSeatingChart == null)
        return;
      if(currentClassFile.isEmpty())
        return;
      
      selectOutput("Select the file to save to:", "constraintFileSaved");
    }
  });
  
  btnClearConstraints = cp5.addButton("ClearConstraints")
    .setLabel("Clear")
    .setPosition(34, 2)
    .setSize(30, 24)
    .moveTo(grpClassFile)
    ;
  addEvent(btnClearConstraints, new CP5EventAction() {
    public void run(ControlEvent e) {
      if(currentClass == null) {
        errorMessage("You must have a class loaded\nto save constraints.");
        return;
      }
      
      for(Student s : currentClass.getStudents()) {
        s.clearAllConstraints();
      }
      
      updateStudentList();
    }
  });
  
  grpSeatingFile = cp5.addGroup("Seating Chart")
    .setLabel("Seating")
    .setBackgroundColor(groupBackgroundColor)
    .setBackgroundHeight(28)
    ;
  
  btnSaveSeatingFile = cp5.addButton("SaveSeating")
    .setLabel("Save")
    .setPosition(2, 2)
    .setSize(30, 24)
    .moveTo(grpSeatingFile)
    ;
  addEvent(btnSaveSeatingFile, new CP5EventAction() {
    public void run(ControlEvent e) {
      if(currentClass == null || currentSeatingChart == null) {
        errorMessage("You must have a class and desk layout loaded\nto save a seating arrangement.");
        return;
      }
      
      selectOutput("Select the file to save to:", "seatingFileSaved");
    }
  });
  
  
  acdFiles = cp5.addAccordion("Files")
    .setPosition(0, 32)
    .setWidth(fileAccordionWidth)
    .setMinItemHeight(1)
    .setCollapseMode(Accordion.MULTI)
    .addItem(grpClassFile)
    .addItem(grpSeatingFile)
    .open()
    ;
}



void setupStudentAccordion() {
  grpRandomize = cp5.addGroup("RandomizeGroup")
    .setLabel("Randomize")
    .setBackgroundColor(groupBackgroundColor)
    .setBackgroundHeight(128)
    ;
  btnRandomize = cp5.addButton("Randomize")
    .setPosition(2, 2)
    .setSize(64, 24)
    .moveTo(grpRandomize)
    ;
  addEvent(btnRandomize, new CP5EventAction() {
    public void run(ControlEvent c) {
      if(currentStudentList == null || currentDeskList == null) {
        println("Nothing to randomize");
        return;
      }
      
      Randomizer r = new RecursiveRandomizer();
      SeatingArrangement result = r.randomize(currentSeatingChart, currentStudentList, rdoFillMode.getState(0), rdoFillMode.getState(1));
      if(result == null) {
        println("ERROR: Couldn't be randomized");
        
        errorMessage("Could not randomize students. Try again.\nIf this happens repeatedly, make sure there isn't an\nimpossible combonation of constraints.");
      } else {
        currentSeatingChart = result;
        updateCurrentSeatingChart();
        for(Desk d : currentDeskList) {
          Student s = d.getStudent();
          if(currentStudentList.contains(s))
            currentStudentList.remove(s);
        }
        selectedStudent = null;
        updateStudentList();
        updateSelectedStudent();
      }
    }
  });
  cp5.addTextlabel("Fill mode:")
    .setText("Fill mode:")
    .setPosition(2, 32)
    .setWidth(width)
    .moveTo(grpRandomize)
    ;
  rdoFillMode = cp5.addRadioButton("Fill Mode")
    .setPosition(2, 48)
    //.setSize(64, 24)
    .addItem("Random", 0)
    .addItem("Bottom-up", 1)
    .addItem("Top-down", 2)
    .activate(0)
    .moveTo(grpRandomize)
    ;
  
  grpStudents = cp5.addGroup("Students")
    .setBackgroundColor(groupBackgroundColor)
    .setBackgroundHeight(studentGroupHeight)
    ;
  cp5.addTextlabel("Selected student:")
    .setText("Selected student:")
    .setPosition(4, 4)
    .setWidth(width)
    .moveTo(grpStudents)
    ;
  lblSelectedStudent = cp5.addTextlabel("SelectedStudent")
    .setPosition(4, 16)
    .setWidth(studentAccordionWidth)
    .moveTo(grpStudents)
    ;
  lstStudents = cp5.addListBox("StudentList")
    .setLabel("Students")
    .setPosition(4, 36)
    .setSize(studentAccordionWidth - 8, studentGroupHeight - 40)
    .moveTo(grpStudents)
    ;
  addEvent(lstStudents, new CP5EventAction() {
    public void run(ControlEvent e) {
      int n = (int)e.getValue();
      if(currentStudentList == null)
        return;
      selectedStudent = currentStudentList.get(n);
      updateSelectedStudent();
    }
  });
  
  
  grpConstraints = cp5.addGroup("Constraints")
    .setBackgroundColor(groupBackgroundColor)
    .setBackgroundHeight(256)
    ;
  btnAllowedDesks = cp5.addButton("Allowed Desks")
    .setPosition(studentAccordionWidth - 80, 7)
    .setSize(78, 16)
    .moveTo(grpConstraints)
    ;
  addEvent(btnAllowedDesks, new CP5EventAction() {
    public void run(ControlEvent e) {
      prepareSetupAllowedDesksGUI();
    }
  });
  lstConstraints = cp5.addListBox("ConstraintList")
    .setLabel("Constraints")
    .setPosition(4, 36)
    .setSize(studentAccordionWidth - 8, 64)
    .moveTo(grpConstraints)
    ;
  addEvent(lstConstraints, new CP5EventAction() {
    public void run(ControlEvent e) {
      int n = (int)e.getValue();
      List<SeatingConstraint> constraintList = selectedStudent.getConstraints();
      if(constraintList.isEmpty())
        return;
      selectedConstraint = constraintList.get(n);
      updateSelectedConstraint();
    }
  });
  
  ddlConstraintMenu = cp5.addDropdownList("NewConstraint")
    .setPosition(4, 7)
    .setWidth(48)
    .setBarHeight(16)
    .setOpen(false)
    //TODO
    //.actAsPulldownMenu(true)
    .moveTo(grpConstraints)
    ;
  int i = 0;
  for(ConstraintType c : constraints) {
    ddlConstraintMenu.addItem(c.toString(), i++);
  }
  ddlConstraintMenu.getCaptionLabel().set("New...");
  ddlConstraintMenu.getCaptionLabel().getStyle().marginTop = 4;
  addEvent(ddlConstraintMenu, new CP5EventAction() {
    public void run(ControlEvent e) {
      ddlConstraintMenu.getCaptionLabel().set("New...");
      int n = (int)e.getValue();
      ConstraintType c = constraints.get(n);
      if(selectedStudent == null)
        return;
      SeatingConstraint constraint = c.create();
      selectedStudent.addConstraint(constraint);
      
      updateStudentList();
      updateConstraintList();
      
    }
  });
  
  btnDeleteConstraint = cp5.addButton("DeleteConstraint")
    .setLabel("Delete")
    .setPosition(58, 7)
    .setSize(48, 16)
    .moveTo(grpConstraints)
    ;
  addEvent(btnDeleteConstraint, new CP5EventAction() {
    public void run(ControlEvent e) {
      if(selectedConstraint == null || selectedStudent == null) {
        return;
      }
      
      selectedStudent.removeConstraint(selectedConstraint);
      selectedConstraint = null;
      
      updateStudentList();
      updateConstraintList();
    }
  });
  
  
  grpConstraintProperties = cp5.addGroup("Properties")
    .setPosition(4, 116)
    .setWidth(studentAccordionWidth - 8)
    .setBackgroundHeight(128)
    .setBackgroundColor(color(95))
    .moveTo(grpConstraints)
    ;
  
  
  
  
  
  acdStudents = cp5.addAccordion("StudentAccordion")
    .setPosition(width - studentAccordionWidth, 24)
    .setWidth(studentAccordionWidth)
    .setMinItemHeight(1)
    .setCollapseMode(Accordion.MULTI)
    .addItem(grpRandomize)
    .addItem(grpStudents)
    .addItem(grpConstraints)
    .open()
    ;
}


void setupErrorGroup() {
  grpError = cp5.addGroup("Error")
    .setPosition(width/2 - 128, height/2 - 64)
    .setWidth(256)
    .setBackgroundHeight(128)
    .setBackgroundColor(groupBackgroundColor)
    ;
  
  lblError = cp5.addTextlabel("ErrorMessage")
    .setText("Error")
    .setPosition(4, 4)
    .moveTo(grpError)
    ;
  
  btnErrorOk = cp5.addButton("OK")
    .setPosition(204, 100)
    .setSize(48, 24)
    .moveTo(grpError)
    ;
  addEvent(btnErrorOk, new CP5EventAction() {
    public void run(ControlEvent e) {
      grpError.setVisible(false);
    }
  });
  
  grpError.setVisible(false);
}

void errorMessage(String message) {
//  error = message;
//  setupErrorMessage = 0;
  JOptionPane.showMessageDialog(null,
    message,
    "Error",
    JOptionPane.ERROR_MESSAGE);
}





void calculateDeskSize(int xLen, int yLen) {
  int screenXLen = width - deskButtonX - studentAccordionWidth;
  int screenYLen = height - deskButtonY;
  
  int size1 = screenXLen / xLen;
  int size2 = screenYLen / yLen;
  
  if(size1 > size2) {
    deskButtonSpacing = size2;
  } else {
    deskButtonSpacing = size1;
  }
  
  deskButtonSize = deskButtonSpacing - 2;
}