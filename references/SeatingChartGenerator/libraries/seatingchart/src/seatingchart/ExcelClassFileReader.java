package seatingchart;

import java.io.*;

import processing.core.*;

import org.apache.poi.hssf.usermodel.*;
import org.apache.poi.poifs.filesystem.*;

public class ExcelClassFileReader implements ClassFileReader {
  public ClassGroup read(PApplet applet, String file) {
    System.out.println("Reading class excel file " + file);
    
    POIFSFileSystem fs;
    
    try {
      fs = new POIFSFileSystem(new FileInputStream(file));
    } catch (IOException ex) {
      System.out.println(ex);
      return null;
    }
    
    try (HSSFWorkbook wb = new HSSFWorkbook(fs)) {
      HSSFSheet sheet = wb.getSheetAt(0);
      
      ClassGroup c = new ClassGroup();
      for(int i = 0; true; i++) {
        String firstName = "";
        String middleName = "";
        String lastName = "";
        String gender = "";
        
        HSSFRow r = sheet.getRow(i + 3);
        if(r == null)
          break;
        
        HSSFCell firstNameCell = r.getCell(0);
        if(firstNameCell == null)
          break;
        firstName = firstNameCell.getStringCellValue();
        if(firstName == null || firstName.isEmpty())
          break;
        
        HSSFCell middleNameCell = r.getCell(1);
        if(middleNameCell != null)
          middleName = middleNameCell.getStringCellValue();
        if(middleName == null)
          middleName = "";
        
        HSSFCell lastNameCell = r.getCell(2);
        if(lastNameCell != null)
          lastName = lastNameCell.getStringCellValue();
        if(lastName == null)
          lastName = "";
        
        HSSFCell genderCell = r.getCell(3);
        if(genderCell != null)
          gender = genderCell.getStringCellValue();
        if(gender == null)
          gender = "";
        
        Student s = new Student();
        s.setFirstName(firstName);
        s.setMiddleName(middleName);
        s.setLastName(lastName);
        switch(Character.toLowerCase(gender.concat(" ").charAt(0))) {
          case 'm':
            System.out.print("Male: ");
            s.setGender(new GenderMale());
            break;
          case 'f':
            System.out.print("Female: ");
            s.setGender(new GenderFemale());
            break;
          default:
            System.out.print("No gender: ");
            s.setGender(new GenderNone());
        }
        
        c.addStudent(s);
        System.out.println(s.getName(NameStyle.FULL));
      }
      
      System.out.println("Done reading file.");
      return c;
    } catch (IOException ex) {
      System.out.println(ex);
      return null;
    }
  }
}
