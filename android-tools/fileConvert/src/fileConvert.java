import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.util.Properties;
import java.util.regex.Pattern;

public class fileConvert {
	private PrintWriter pw = null;
	private int mCounter = 0;
	private String mLine = null;

	public fileConvert(String file) {

		System.out.println("Parse file from '" + file + "'");
		int lastCounter;
		File source = null;
		BufferedReader br = null;
		try {
			source = new File(file);
			br = new BufferedReader(new FileReader(source));
		} catch (IOException e) {
			e.printStackTrace();
			System.exit(-1);
		}

		if (br != null) {

			try {
				pw = new PrintWriter(new File("output/"+ file));
			} catch (FileNotFoundException e1) {
				e1.printStackTrace();
				System.exit(-1);
			}

			if (pw != null) {
				try {
					while ((mLine = br.readLine()) != null) {
						final String line = mLine;
                                                System.out.println("line: '" + mCounter + "'"+line);
						mCounter++;
				                pw.println(line.replaceAll("^[0-9]{1,4}", ""));
					}
				} catch (IOException e) {
					e.printStackTrace();
					System.exit(-1);
				}
				pw.close();
			}
		}
	}

	public static void main(String[] args) {
		if (args.length == 1) {
			new fileConvert(args[0]);
		} else {
			System.out
					.println("usage: java IconVisualizer runtime|device_log_path line_per_block");
		}
	}

}
