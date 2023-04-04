import java.io.File;
import java.io.FileNotFoundException;
import java.util.ArrayList;
import java.util.Scanner;

public class JukeboxHero

{
	/* Displays main menu and accepts user input, swapping to the appropriate case, each of which is outlined below. In abstract: 
	 * "Load" allows the user to input a .csv file for the program to parse and analyze
	 * "Search" allows the user to enter a single query and returns any song, album, or artist containing that query.
	 * "Analyze" tells the user how many artists, albums, and songs are contained in the playlist, as well as its total time
	 * "Print" writes out the entire playlist as a set of strings.
	 * @Author: James Crowell
	 */

	public static void main(String[] args)

	{

		// Input, processing variable
		Scanner userChooser = new Scanner(System.in);
		String userChoosed = "";
		String loadDialogue = "";

		// Menu Display Loop Condition
		boolean onScreen = true;

		// Headings
		String border = "**********************************************************************************";
		String titleTop = "*				JukeBox Hero	V1.0		 		 *";
		String titleBottom = "*				     Main Menu		   	     	         *";
		String printOutHead = "Title \t \t \t \t Artist \t \t Album \t \t \t Playtime";

		// ArrayList for data processing
		ArrayList<Song> songList = new ArrayList<Song>();

		// This is an error we'll need several uses of.
		String noBox = "There is currently no playlist loaded! Please load a playlist and try again.";

		// Initialize to main menu
		System.out.println(border);
		System.out.println(titleTop);
		System.out.println(titleBottom);
		System.out.println(border);
		System.out.println("(L)oad Catalog");
		System.out.println("(S)earch Catalog");
		System.out.println("(A)nalyse Catalog");
		System.out.println("(P)rint Catalog");
		System.out.println("(Q)uit");

		while (onScreen) {
			System.out.println("Please choose an option above, or press 'm' for Menu.");

			userChoosed = userChooser.nextLine().toLowerCase();

			switch (userChoosed) {

			// Load protocol. Sets up our delimiters, takes in a .csv file, records data of
			// the .csv's elements, rearranges it,
			// and creates a new list with it.
			case "l":
				// Empty song list every time "load" is called, so we don't add to it; we
				// replace it
				songList.clear();

				// Prompt
				System.out.println("Load which catalog file? \n");

				// Assuming a successful load, we'll need to set the proper delimiters for the
				// scanner.
				final String SEPERATOR = ",";

				// Grab user's choice, turn it into a file; pass it into the scanner
				// (Combinable?).
				loadDialogue = userChooser.nextLine();

				File listChoice = new File(loadDialogue);
				try {
					// Scanner which reads the input file
					Scanner listReader = new Scanner(listChoice);

					while (listReader.hasNextLine()) {
						String rawLine = listReader.nextLine();
						Scanner stringSplitter = new Scanner(rawLine).useDelimiter(SEPERATOR);
						String inArtist = stringSplitter.next();
						String inAlbum = stringSplitter.next();
						String inTitle = stringSplitter.next();
						int inTime = stringSplitter.nextInt();
						stringSplitter.close();

						Song newAdd = new Song(inTitle, inArtist, inAlbum, inTime);
						songList.add(newAdd);
					}
					listReader.close();
					System.out.println(songList.size() + " songs loaded!");
				} catch (FileNotFoundException e) {
					System.out.println("Unable to load. Confirm file is in directory and entered filename is correct.");
				}

				break;

			// Search function. Creates ArrayLists for Artist and Album, checks unique
			// values among them in songList and parses
			// accordingly.
			case "s":
				if (songList.isEmpty())
					System.out.println(noBox);
				else {
					// Variable that we'll check against playlist contents
					String userSearch = "";

					// Take in search term, convert to lowercase, which we'll also do when
					// comparing.
					System.out.println("Search for what term?");
					userSearch = userChooser.nextLine().toLowerCase();

					// Create additional Array List that we'll print out, if we can populate it.
					ArrayList<Song> searchResults = new ArrayList<Song>();

					// Step through the loaded list, seeing if the search query matches any part of
					// the list's elements.
					// This will allow searching for words in Titles (including subwords of compound
					// words; i.e. "grape"
					// in "grapefruit"), Artists, or albums
					for (Song memberSong : songList) {
						// Ensure compared elements are all lowercase as well; search is now
						// case-insensitive.
						String songCheck = memberSong.toString().toLowerCase();
						if (songCheck.contains(userSearch))
							searchResults.add(memberSong);
					}

					// If we don't turn anything up
					if (searchResults.isEmpty())
						System.out.println("No results found!");

					// If we do
					else {
						System.out.println("Found " + searchResults.size() + " matches:");
						for (Song searchedSong : searchResults)
							System.out.println(searchedSong.toString());
					}
				}
				break;

			// Analysis
			case "a":
				// Once again, in case of no playlist having been loaded:
				if (songList.isEmpty())
					System.out.println(noBox);
				else {

					// ArrayLists we'll need for parsing
					ArrayList<String> artistList = new ArrayList<String>();
					ArrayList<String> albumList = new ArrayList<String>();

					// Total playtime
					int overAllPlay = 0;

					for (Song memberSong : songList) {
						overAllPlay = overAllPlay + memberSong.getPlayTime();
						if (!artistList.contains(memberSong.getArtist()))
							artistList.add(memberSong.getArtist());
						if (!albumList.contains(memberSong.getAlbum()))
							albumList.add(memberSong.getAlbum());

					}

					System.out.println("The currently loaded playlist contains:");
					System.out.println(border);
					System.out.println("Artist Number: " + artistList.size());
					System.out.println("Album Number: " + albumList.size());
					System.out.println("Song Number: " + songList.size());
					System.out.println("Total Playlist Time in Seconds: " + overAllPlay);
				}

				break;

			// Print function. If we have a loaded list, run a foreach loop and String 'em
			// out.
			case "p":

				if (songList.isEmpty())
					System.out.println(noBox);
				else {
					System.out.println("The playlist currently loaded contains " + songList.size() + " songs:");
					System.out.println(border);
					System.out.println(printOutHead);
					System.out.println(border);
					for (Song newAdd : songList) {
						System.out.println(newAdd.toString());
					}
				}

				break;

			// Menu. While displayed upon initialization, this produces the "M" user choice
			// response.
			case "m":
				System.out.println(border);
				System.out.println(titleTop);
				System.out.println(titleBottom);
				System.out.println(border);
				System.out.println("(L)oad Catalog");
				System.out.println("(S)earch Catalog");
				System.out.println("(A)nalyze Catalog");
				System.out.println("(P)rint Catalog");
				System.out.println("(Q)uit");

				break;

			// Quit.
			case "q":
				userChooser.close();
				System.out.println("See you next time!");
				onScreen = false;
				break;

			default:
				System.out.println("Invalid selection!");
			}

		}

	}

}
