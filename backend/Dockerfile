FROM japeto/so-tools:v64

WORKDIR /app
COPY server.cpp .
RUN g++ server.cpp -o server -pthread
EXPOSE 5000

CMD ["./server"]