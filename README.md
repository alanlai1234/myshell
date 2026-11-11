# myshell
Alan Lai, kl1210,Cheng Chan Lee, cl1814

Case	What we tested	Expected result

01	Pathname	/bin/echo PATH_OK prints PATH_OK.

02	Bare name	echo BARE_NAME_OK finds echo and prints BARE_NAME_OK.

03	Built-in pwd	Prints the starting working directory.

04	Built-in which	Prints the path found for echo, then prints /bin/echo for the explicit pathname. Both were /bin/echo on our Mac.

05	Execute permission	which reports that the test file has no executable match. The next line prints CONTINUED_AFTER_FAILED_WHICH.

06	Built-in cd and pwd	pwd shows p3-test-fixtures/sub under the starting directory, then shows the starting directory again after cd ../...

07	Wildcard	wild-*.txt matches wild-.txt and wild-b.txt. The .log file is excluded.

08	Input redirection	cat < reads the input file and prints INPUT_OK.

09	Output redirection and append	> replaces OLD with NEW, and >> adds APPENDED on the next line.

10	One pipe	echo PIPE_OK | cat prints PIPE_OK.

11	Three-command pipeline	echo pipeline | tr a-z A-Z | cat prints PIPELINE.

12	Wildcard and pipe	cat reads the files matched by the wildcard and sends their contents to sort. The output is A followed by B on separate lines.

13	Input and output redirection together	cat copies the input file using < and >. Reading the copy prints INPUT_OK.

14	Built-in exit	exit ignored_argument ends the shell with status 0. SHOULD_NOT_RUN never appears.
