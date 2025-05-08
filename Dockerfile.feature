FROM php:8.3-cli

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    autoconf \
    git \
    vim \
    && rm -rf /var/lib/apt/lists/*

# Clone the phpredis feature/vector-sets branch from your repository
WORKDIR /usr/src
RUN git clone -b feature/vector-sets https://github.com/mferrara/phpredis.git 

# Work in the phpredis directory
WORKDIR /usr/src/phpredis

# Verify the vector_set_commands.c file exists
RUN echo "Checking for vector_set_commands.c..." && \
    ls -la vector_set_commands.c && \
    echo "Checking if included in config.m4..." && \
    grep -i "vector_set_commands.c" config.m4

# Build the extension
RUN phpize && \
    ./configure && \
    make && \
    make install && \
    docker-php-ext-enable redis

# Copy test scripts
COPY ./test_vector_sets.php /usr/src/phpredis/
COPY ./test_vadd_format.php /usr/src/phpredis/
COPY ./test_vector_binary.php /usr/src/phpredis/
COPY ./test_both_formats.php /usr/src/phpredis/

# Default command: run both formats test script
CMD ["php", "-d", "display_errors=1", "test_both_formats.php"]