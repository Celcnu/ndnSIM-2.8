**如果使用模板, 必须在clone代码后执行以下操作:**

## (1) Build and install NS-3 and ndnSIM
	`cd ns-3`
	`./waf configure -d optimized`
	`./waf`

## (2) scenario setting
	`sudo ./waf install`
	`cd ../scenario`
	`export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig`
	`export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH`
	`./waf configure` 
	`./waf --run <scenario>`


