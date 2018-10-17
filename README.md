# TCPServerMultiThread

#  Features:

    -Multi-threaded : 
    ->1 thread for listenner 
    ->Each client is a separate thread)
    -Uses read buffer queue to avoid blocking of execution
    -Uses write buffer queue to avoid blocking of execution


	#TODO
  
	Use std::future instead atomic for thread term signaling

	#Possible optimizations:
  
	  -Use libevent to avoid select
	  -Pre-allocate a thread pool for incoming connections
	  -Set a maximum range for new threads & partition/assign active connections
	  -Reduce allocations by using either a faster allocator or implement custom pool
  
