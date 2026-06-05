ClassGroup currentClass;
List<Student> currentStudentList;

Student selectedStudent;
SeatingConstraint selectedConstraint;

List<ControllerInterface> currentPropertyControls;

SeatingArrangement currentSeatingChart;
List<Desk> currentDeskList;

String currentClassFile;

void setupState() {
  println("Setup state...");
  currentStudentList = new ArrayList<Student>();
  currentClass = null;
  currentSeatingChart = null;
  selectedStudent = null;
}


void clearAllDesks() {
  if(currentDeskList == null) {
    return;
  }
  
  for(Desk d : currentDeskList) {
    if(d.getStudent() != null) {
      currentStudentList.add(d.getStudent());
      d.setStudent(null);
    }
  }
      
  updateStudentList();
  updateDesks();
}


void updateCurrentClass() {
  println("Updating class...");
  currentStudentList = new ArrayList<Student>(currentClass.getStudents());
  
  updateStudentList();
  
  selectedStudent = null;
  updateSelectedStudent();
}

void updateSelectedStudent() {
  println("Updating selected student...");
  
  lstStudents.setColor(ControlP5.THEME_CP5BLUE);
  
  String selectedStudentName = "";
  if(selectedStudent != null) {
    selectedStudentName = selectedStudent.getName(NameStyle.FULL);
    int n = currentStudentList.indexOf(selectedStudent);
    //TODO
    //ListBoxItem item = lstStudents.getItem(n);
    //item.setColorBackground(listSelectedColor);
  }
  lblSelectedStudent.setText(selectedStudentName);
  
  selectedConstraint = null;
  updateConstraintList();
}


void updateSelectedConstraint() {
  println("Updating selected student...");
  
  lstConstraints.setColor(ControlP5.THEME_CP5BLUE);
  
  if(selectedConstraint != null) {
    int n = selectedStudent.getConstraints().indexOf(selectedConstraint);
    //TODO
    //ListBoxItem item = lstConstraints.getItem(n);
    //item.setColorBackground(listSelectedColor);
  }
  
  updateConstraintGUI();
}

void updateCurrentSeatingChart() {
  println("Updating seating chart...");
  currentDeskList = new ArrayList<Desk>(currentSeatingChart.getDesks());
  
  updateDesks();
}


void updateDesks() {
  if(currentSeatingChart != null) {
    if(currentSeatingChart.getXSize() > 0 && currentSeatingChart.getYSize() > 0) {
      calculateDeskSize(currentSeatingChart.getXSize(), currentSeatingChart.getYSize());
    }
  }
  
  setupDeskButtons = 0;
}

void updateStudentList() {
  setupStudentList = 0;
}

void updateConstraintList() {
  setupConstraintList = 0;
}

void updateConstraintGUI() {
  setupConstraintGUI = 0;
}