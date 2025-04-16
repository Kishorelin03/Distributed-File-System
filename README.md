
  <h1>Distributed File System for COMP8567</h1>
  
  <div class="section">
    <h2>Project Overview</h2>
    <p>This repository contains the source code for a distributed file system project developed as part of COMP8567. The system is designed to handle file operations in a distributed environment by separating responsibilities between a main server and various backend servers based on file type.</p>
    <ul>
      <li>
        <strong>Main Server (S1):</strong>  
        Listens for client connections on port <code>7010</code> and handles file operations (upload, download, delete, create tar archives, and display file names). C source files (<code>.c</code>) are processed locally; all other file types (such as <code>.pdf</code>, <code>.txt</code>, and <code>.zip</code>) are forwarded to backend servers.
      </li>
      <li>
        <strong>PDF Backend Server (S2):</strong>  
        Runs on port <code>7100</code> and manages PDF file operations.
      </li>
      <li>
        <strong>TXT Backend Server (S3):</strong>  
        Runs on port <code>7200</code> and handles text file operations.
      </li>
      <li>
        <strong>ZIP Backend Server (S4):</strong>  
        Runs on port <code>7300</code> and handles operations for ZIP files.
      </li>
      <li>
        <strong>Client Program (w25clients.c):</strong>  
        A command-line client that connects to the main server (S1) and sends commands such as upload, download, remove, and file listing.
      </li>
    </ul>
  </div>
  
  <div class="section">
    <h2>Features</h2>
    <ul>
      <li><strong>File Upload:</strong>  
          Use the <code>uploadf</code> command to upload files. Files with a <code>.c</code> extension are stored locally by S1, while other files are forwarded to the appropriate backend servers.
      </li>
      <li><strong>File Download:</strong>  
          Use the <code>downlf</code> command to download individual files. Local <code>.c</code> files are served directly by S1; other files are requested from the appropriate backend servers.
      </li>
      <li><strong>Tar Archive Download:</strong>  
          The <code>downltar</code> command allows downloading a tar archive of files. S1 generates the archive locally for <code>.c</code> files and forwards requests for other types to the relevant backend servers.
      </li>
      <li><strong>File Removal:</strong>  
          The <code>removef</code> command deletes files. S1 handles <code>.c</code> files locally, and for other types, the request is forwarded to the appropriate backend.
      </li>
      <li><strong>File Listing:</strong>  
          The <code>dispfnames</code> command aggregates a sorted list of file names from local storage and backend servers.
      </li>
    </ul>
  </div>
  
  <div class="section">
    <h2>Installation and Compilation</h2>
    <p>Ensure you are on a POSIX-compliant system (e.g., Linux) with a standard C compiler (such as GCC) installed.</p>
    <pre><code>
# Compile the main server (S1)
gcc -o S1 S1.c

# Compile the PDF backend server (S2)
gcc -o S2 S2.c

# Compile the TXT backend server (S3)
gcc -o S3 S3.c

# Compile the ZIP backend server (S4)
gcc -o S4 S4.c

# Compile the client program
gcc -o w25clients w25clients.c
    </code></pre>
  </div>
  
  <div class="section">
    <h2>Usage</h2>
    <h3>Starting the Servers</h3>
    <ol>
      <li><strong>Main Server (S1):</strong>
        <pre><code>./S1</code></pre>
      </li>
      <li><strong>Backend Servers:</strong>
        <ul>
          <li>PDF Backend (S2): <pre><code>./S2</code></pre></li>
          <li>TXT Backend (S3): <pre><code>./S3</code></pre></li>
          <li>ZIP Backend (S4): <pre><code>./S4</code></pre></li>
        </ul>
      </li>
    </ol>
    <h3>Running the Client</h3>
    <pre><code>./w25clients</code></pre>
    <p>After running the client, you will see a prompt (e.g., <code>w25clients$</code>). You can then use commands such as:</p>
    <ul>
      <li><code>uploadf myfile.c ~S1/folder</code> – Uploads a C file. Other file types are forwarded.</li>
      <li><code>downlf ~S1/folder/myfile.c</code> – Downloads an individual file.</li>
      <li><code>downltar .c</code> – Downloads a tar archive of all C files. Use <code>.pdf</code>, <code>.txt</code>, or <code>.zip</code> for backend files.</li>
      <li><code>removef ~S1/folder/myfile.c</code> – Deletes a specified file.</li>
      <li><code>dispfnames ~S1/folder</code> – Displays a sorted list of file names aggregated from local storage and backend servers.</li>
      <li><code>exit</code> – Exits the client interface.</li>
    </ul>
  </div>
  
  <div class="section">
    <h2>Project Structure</h2>
    <ul>
      <li><strong>S1.c:</strong> Main server code handling client connections and file operations.</li>
      <li><strong>S2.c:</strong> PDF backend server handling PDF file operations.</li>
      <li><strong>S3.c:</strong> TXT backend server managing text file operations.</li>
      <li><strong>S4.c:</strong> ZIP backend server managing ZIP file operations.</li>
      <li><strong>w25clients.c:</strong> Client program providing a command-line interface for interactions with the distributed file system.</li>
    </ul>
  </div>
  
  <div class="section">
    <h2>Future Enhancements</h2>
    <ul>
      <li>Extend support for additional file types or more robust routing between servers.</li>
      <li>Improve security by adding authentication and encryption.</li>
      <li>Enhance error handling and add comprehensive logging for production use.</li>
      <li>Develop a graphical or web-based interface for easier management and interaction.</li>
    </ul>
  </div>

