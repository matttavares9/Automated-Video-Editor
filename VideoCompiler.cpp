#include <iostream>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/video/tracking.hpp>
#include <iostream>
#include <math.h>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <vector>
#include <limits>
#include <cstdlib>

#include "Clip.h"

//Namespaces
using namespace cv;
using namespace std;

//Fixes the input path by ensuring it includes an extra backslash.
//In C++, a single backslash "\" is as an escape character, which can lead to path-related issues. This function adds an extra backslash in the path to ensure proper formatting.
string fixDirectory(const string& path)
{
	return (std::filesystem::path(path) / "").string();
}

//Checks if the given input string represents a valid integer number.
bool isNumber(const string& input) {
	try {
		stoi(input);
		return true;
	}
	catch (const exception&) {
		return false;
	}
}

//Checks if a video file with the given name exists at the specified path.
//"name" represents the name of the video file to check.
//"path" represents the directory path where the video file should be located.
//Returns true if the video file exists at the provided path, otherwise returns false.
bool doesFileExist(const string& name, const string& path) {
	string new_path = path;
	new_path = fixDirectory(new_path); //New path fixes the directory to avoid escape character issues.
	string file_location = new_path + name;
	ifstream video(new_path + name);

	if (video.is_open() == true) //If the video file is found and opened, returns true.
		return true;
	else
		return false;
}

//Parses the text file containing the path and video file names. Returns a vector to store this information.
//"readingFile" is the .txt filename containing the video information.
//Returns a vector containing the data.
vector<string> parse(const string& readingFile)
{
	ifstream inputFile(readingFile);
	vector<std::string> names;
	string line;

	int checks = 0;

	//Loops through every line in the text file.
	while (getline(inputFile, line)) {
		if (checks == 0) { //If it's the first line. The firt line contains the path.
			string path = fixDirectory(line);
			names.push_back(path);
		}
		else
		{
			names.push_back(line);
		}
		checks++;
	}

	return names;
}

//Function which uses OpenCV to display the clips in succession in a separate window.
//"head" points to the first element in the linked list of Clip objects.
void userDisplay(Clip* head)
{
	Clip* display = head;
	while (display != NULL)
	{
		display->Display();
		display = display->next;
	}
}

//Allows the user to edit the start and end times of video clips.
//"head" pointer to the first clip in the linked list.
//"max_id" is the last element's ID.
void userEdit(Clip* head, int max_id)
{
	bool is_editing = true;
	while (is_editing == true)
	{
		string clip_to_edit;
		cout << "Enter clip number to edit: ";
		cin >> clip_to_edit;

		//Check to ensure user enters valid clip number.
		if (stoi(clip_to_edit) <= max_id)
		{
			string new_start;
			string new_end;

			cout << endl << "<ENTER> to skip" << endl << "Start: ";
			cin >> new_start;

			cout << "End: ";
			cin >> new_end;

			//Checks that user entered a valid number
			if (new_start.length() > 0 && isNumber(new_start))
			{
				Clip* update = head;
				int clip_selected = stoi(clip_to_edit);

				//Traverses through linked list until the clip number is found. Then, data is updated if valid.
				while (update->id != clip_selected)
				{
					update = update->next;
				}
				if (update->getLength() > stoi(new_start))
					update->setStart(stoi(new_start));
				else
					cout << endl << "Invalid start";
			}

			if (new_end.length() > 0 && isNumber(new_end))
			{
				Clip* update = head;
				int clip_selected = stoi(clip_to_edit);

				while (update->id != clip_selected)
				{
					update = update->next;
				}
				if (update->getLength() > stoi(new_end))
					update->setEnd(stoi(new_end));
				else
					cout << endl << "Invalid end";
			}
		}
		else
			cout << "Invalid clip";

		string option;
		std::cout << endl << "Continue? <Y/N>" << endl;
		std::cin >> option;

		//User input options to continue or exit the editing loop.
		if (option == "y" || option == "Y")
			is_editing = true;
		if (option == "n" || option == "N")
			is_editing = false;
		else
			is_editing = false;
	}
}

//Traverses the linked list to add Clips to the VideoWriter. Then, a video file is output in .avi video codec.
//"head" pointer to first clip.
//"output" is the VideoWriter object which will be writing the image sequences.
void userRender(Clip* head, cv::VideoWriter& output)
{
	Clip* itr = head;
	while (itr != NULL)
	{
		std::cout << "Rendering clip " << itr->id << "..." << endl;
		itr->WriteTo(output);
		itr = itr->next;
		std::cout << "Finished." << endl;
	}
}

int main()
{
	//Handles the file names
	string filename;
	string name_of_file;

	//Check for the loop to ensure the user input is valid
	bool valid_selection = false;

	while (valid_selection == false)
	{
		string file_choice; //User's choice to either create or use existing .txt file.
		string filename; //Name of the file.

		/*
		* Gives user the option to create a text file containing data for the video trimmer.
		* If user creates <C> the text file, they can add information for a directory and subsequent clip names.
		* If user selects an existing <E> text file, they can use an previously created text file stored with video editing data.
		* In the text file: First line is the path; all subsequent lines are video file names.
		*/
		cout << "Create or use existing .txt file? <C/E>" << endl;
		cin >> file_choice;

		if (file_choice == "C" || file_choice == "c") {
			cout << "Enter new file name: " << endl;

			cin >> filename;
			filename = filename + ".txt";
			name_of_file = filename;

			ofstream readingFile(filename);

			string video_path;
			cout << "Enter path to clips: " << endl;

			//Ensures that the entire line is added to the video path to avoid spacing issues.
			cin.ignore(numeric_limits<streamsize>::max(), '\n');
			getline(cin, video_path);

			readingFile << video_path;
			cout << "Add clip names to project: (Enter <Q> to stop)" << endl;
			string clip_name;
			cin >> clip_name;

			//User can add clips until "q" is pressed.
			while (clip_name != "Q" && clip_name != "q")
			{
				if (doesFileExist(clip_name, video_path) == true) //Makes sure video file exists before adding it to the text file.
					readingFile << "\n" + clip_name;
				else
				{
					cout << "Video file not found at path.";
				}
				cin >> clip_name;
			}
			valid_selection = true;
		}
		//Handles when user already has existing file.
		else if (file_choice == "E" || file_choice == "e") {
			cout << "Enter file name: " << endl;
			cin >> filename;
			filename = filename + ".txt";
			name_of_file = filename;
			valid_selection = true;
		}
		else {
			cout << "Invalid selection." << endl;
		}
	}
	cout << endl;

	//Creates a file to redirect the console output. OpenCV displays unneccessary information to the console. This is removed for readability.
	ofstream file("output.txt");
	streambuf* coutBuffer = cout.rdbuf();
	cout.rdbuf(file.rdbuf());

	//Pointers creates to traverse through linked list of Clip objects. Each Clip object stores data about each video file/clip.
	int clip_num = 0;
	Clip* clip = nullptr;
	Clip* head = nullptr;

	//Vector containing video file names and it's path.
	vector<string> names = parse(name_of_file);
	string path = names[0];

	//Loops through the number of video names added into the text file.
	if (names.size() > 0)
	{
		for (int i = 1; i < names.size(); i++) //Starts at 1 to ignore the path name.
		{
			string video_file = names[i];
			Clip* curr = new Clip(video_file, clip_num, path); //Creates Clip object and adds it to the heap for dynamic memory allocation. Linked list is then built.
			if (const char* cascade = std::getenv("FACE_CASCADE_PATH")) curr->setFaceCascade(cascade);

			if (clip == nullptr)
			{
				clip = curr;
				head = clip;
			}
			else
			{
				clip->next = curr;
				clip = clip->next;
			}
		}
	}
	cout.rdbuf(coutBuffer); //Ends the console output redirection.
	ofstream outputFile("junk.txt");

	//Pointer to clip object for iteration through the linked list.
	Clip* itr = head;
	int max_id = 0;
	while (itr != NULL)
	{
		cout << "Processing clip " << itr->id << "..." << endl;

		//Rewrites opencv console messages to output file
		streambuf* originalOutput = cout.rdbuf();
		cout.rdbuf(outputFile.rdbuf());

		//Performs the video processing in Clip object and iterates through each clip in the list.
		itr->Create();
		max_id = itr->id;
		itr = itr->next;

		cout.rdbuf(originalOutput);

		cout << "Finished." << endl;
	}

	//USER'S CHOICE TO DISPLAY, EDIT OR RENDER

	itr = head; //Set back to first position/head of linked list.
	string userInput;
	cout << endl << "Display, Edit or Render? <D/E/R>" << endl;
	cin >> userInput;
	while (true)
	{
		if (userInput == "d" || userInput == "D")
			userDisplay(itr);
		else if (userInput == "e" || userInput == "E")
			userEdit(itr, max_id);
		else if (userInput != "r" || userInput != "R")
			break;
		else
			cout << "Invalid selection" << endl;

		cout << endl << "Display, Edit or Render? <D/E/R>" << endl;
		cin >> userInput;
	}
	itr = head;

	//Uses the OpenCV VideoWriter to write a video file from image sequences in .avi format..
	VideoWriter Output("Test.avi", VideoWriter::fourcc('M', 'J', 'P', 'G'), clip->getFPS(), Size(clip->getWidth(), clip->getHeight()));
	userRender(itr, Output);

	//Deallocates dynamic memory to prevent memory leaks.
	while (itr != nullptr) {
		Clip* next_clip = itr->next; //Store the next pointer before deleting.
		delete itr; //Delete the current Clip object.
		itr = next_clip; //Move to the next Clip object.
	}

	head = nullptr;
}
