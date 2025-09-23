# Use the official Ubuntu as the base image
FROM ubuntu:latest


# Update the package lists, install essential packages, and clean up
RUN apt update \
&& apt install -y sudo gcc g++ python3 wget nano p7zip p7zip-full zip unzip  \
&& rm -rf /var/lib/apt/lists/*


# Set the working directory
WORKDIR /root

# Copy file into image
COPY . .

# Add the current directory (.) to the PATH environment variable
ENV PATH="${PATH}:."


# Start a Bash shell when the container is run
CMD ["/bin/bash"]