//---------------------------------------------------------------------------
// Technical functions v1.10 feb 2011
//---------------------------------------------------------------------------
//
// What's new:
// ver 1.10 (feb 2011 update by Wessel de Roode)
//
// 1. Added function iGetPrice(PChar Symbol, int TimeFrame, int index, TPriceType PriceType)
//    Get price by index and type on a specific time frame
// 2. Added function iGetMA(PChar Symbol, int TimeFrame, int index,int shift, int period, TMAType maType, TPriceType ApplyTo,double prev = 0)
//    Get price by index and type on a specific time frame
// 3. Added GetMA(TIndexBuffer priceBars, int index, int shift, int period, TMAType maType, double prev)
//    GetMA Implementation based on TIndexBuffer
// 4. Add GetATR(int index, TPriceType ApplyToPrice,  int atrPeriod, TIndexBuffer ATRTempBuffer)
//    Get Average True Range

#define MaxInt (int)(2147483647)

enum TPriceType {pt_Close, pt_Open, pt_High, pt_Low, pt_HL2, pt_HLC3, pt_HLCC4};
enum TMAType {ma_SMA, ma_EMA, ma_WMA, ma_SSMA};
enum TValueType {vt_Open, vt_High, vt_Low, vt_Close, vt_Volume};


// Get Average True Range
// index - index in bars array
// ApplyTo - price type
// atrPeriod - period for ATR
// ATRTempBuffer - historic value's of ATR
double GetATR(int index, TPriceType ApplyToPrice,  int atrPeriod, TIndexBuffer ATRTempBuffer);

// Get price by index and type
// index - index in bar array
// PriceType - type of price or combination
double GetPrice(int index, TPriceType PriceType);

// Get price by index and type on a specific time frame
// Symbol - requested currency
// TimeFrame - requested timeframe
// index - index in bar array
// PriceType - type of price or combination
double iGetPrice(PChar Symbol, int TimeFrame, int index, TPriceType PriceType);

// Register option "Apply to price" and fill array of its
// text values
void RegApplyToPriceOption(int& option, PChar name = "");

// Register option "MA type" and fill array of its text values
void RegMATypeOption(int& option, PChar name = "");

// Get linear regression channel parameters
// shift - shift value
// period - channel period
// PriceType - type of price
// StartValue - start price of center line
// EndValue - end price of center line
// Height - channel height
// top - channel top
// bottom - channel bottom
void LRCChannelParams(int shift, int period, TPriceType PriceType,
  double& StartValue, double& EndValue, double& Height, double& Top, double& Bottom);

// Get moving average value
// index - index in bars array
// shift - shift value
// maType - moving average type (SMA, EMA, WMA)
// ApplyTo - price type
// prev - previous value for EMA
double GetMA(int index, int shift, int period, TMAType maType, TPriceType ApplyTo, double prev = 0);

// Get moving average value based on a TIndexBuffer 
// priceBars - Source price bars for calculation
// index - index in bars array
// shift - shift value
// maType - moving average type (SMA, EMA, WMA)
// prev - previous value for MA
double GetMA(TIndexBuffer priceBars, int index, int shift, int period, TMAType maType, double prev = 0);

// Get price by index and type on a specific time frame
// index - index in bar array
// PriceType - type of price or combination
// double iGetPrice(PChar Symbol, int TimeFrame, int index, TPriceType PriceType);
// Get moving average value
// Symbol - requested currency
// TimeFrame - requested timeframe
// index - index in bars array
// shift - shift value
// period - MA period
// maType - moving average type (SMA, EMA, WMA)
// ApplyTo - price type
// prev - previous value for MA
double iGetMA(PChar Symbol, int TimeFrame, int index,
                 int shift, int period, TMAType maType, 
                 TPriceType ApplyTo,double prev = 0);

// Get highest value in array
// ValueType - type of the value (vt_Open..vt_Volume)
// StartIndex - index from which we start search
// count - number of bars to search
double GetHighestValue(TValueType ValueType, int StartIndex, int count);

// Get lovest value in array
// ValueType - type of the value (vt_Open..vt_Volume)
// StartIndex - index from which we start search
// count - number of bars to search
double GetLowestValue(TValueType ValueType, int StartIndex, int count);

// IMPLEMENTATIONS
//////////////////

// GetATR Implementation
double GetATR(int index, TPriceType ApplyToPrice,  int atrPeriod, TIndexBuffer ATRTempBuffer){

	double P_High = High(index);
	double P_Low  = Low(index);
	
	if (index = Bars() - 1){
		ATRTempBuffer[index] = P_High - P_Low;
	} else {
		double prevclose = GetPrice(index + 1, TPriceType(ApplyToPrice));
        ATRTempBuffer[index]  = max(P_High, prevclose) - min(P_Low, prevclose);
	}

  double sum = 0;
  for( int i=0; i<= atrPeriod - 1;i++){
    sum = sum + ATRTempBuffer[index + i];
  }
  return sum/atrPeriod;
}

//-----Get price---------------------------------------------------------------
double GetPrice(int index, TPriceType PriceType)
{
  switch (PriceType)
  {
    case pt_Close: return Close(index);
    case pt_Open:  return Open(index);
    case pt_High:  return High(index);
    case pt_Low:   return Low(index);
    case pt_HL2:   return (High(index) + Low(index))/2;
    case pt_HLC3:  return (High(index) + Low(index) + Close(index))/3;
    case pt_HLCC4: return (High(index) + Low(index) + Close(index)*2)/4;
  }

  return 0;
}

//----- iGetPrice ---------------------------------------------------------------
double iGetPrice(PChar Symbol, int TimeFrame, int index, TPriceType PriceType){

  switch(PriceType){
	case pt_Close:  return(iClose(Symbol,TimeFrame,index));
	case pt_Open: return(iOpen(Symbol,TimeFrame,index));
	case pt_High: return(iHigh(Symbol,TimeFrame,index));
	case pt_Low:  return(iLow(Symbol,TimeFrame,index));
	case pt_HL2:   return((iHigh(Symbol,TimeFrame,index)
                        +iLow(Symbol,TimeFrame,index))/2);
	case pt_HLC3:  return((iHigh(Symbol,TimeFrame,index)
                        + iLow(Symbol,TimeFrame,index)
                        + iClose(Symbol,TimeFrame,index))/3);
	case pt_HLCC4: return((iHigh(Symbol,TimeFrame,index)
                        + iLow(Symbol,TimeFrame,index)
                        + iClose(Symbol,TimeFrame,index)*2)/4);
	default: return(0.0);
  }
}

//-----Create "Apply to price" option------------------------------------------
void RegApplyToPriceOption(int& option, PChar name)
{
  if (strcmp(name, "") == 0) name = "Apply to price";
  RegOption(name, ot_EnumType, &option);
  AddOptionValue(name, "Close");
  AddOptionValue(name, "Open");
  AddOptionValue(name, "High");
  AddOptionValue(name, "Low");
  AddOptionValue(name, "(High + Low)/2");
  AddOptionValue(name, "(High + Low + Close)/3");
  AddOptionValue(name, "(High + Low + Close + Close)/4");
  option = 0;
}

//-----Create "MA type" option-------------------------------------------------
void RegMATypeOption(int& option, PChar name)
{
  if (strcmp(name, "") == 0) name = "MA type";
  RegOption(name, ot_EnumType, &option);
  AddOptionValue(name, "Simple (SMA)");
  AddOptionValue(name, "Exponential (EMA)");
  AddOptionValue(name, "Weighted (WMA)");
  AddOptionValue(name, "Smoothed (SSMA)");
  option = 0;
}

//-----Get channel params------------------------------------------------------
void LRCChannelParams(int shift, int period, TPriceType PriceType,
  double& StartValue, double& EndValue, double& Height, double& Top, double& Bottom)
{
  int i, x;
  double a, b, y, z, sum_x, sum_y, max, sum_xy, sum_x2;

  // Variable initialization
  sum_x = 0;
  sum_y = 0;
  sum_xy = 0;
  sum_x2 = 0;

  // Calculating sums for regression line
  i = shift;
  for (x=0; x < period; x++)
  {
    y = GetPrice(i, PriceType);
    sum_x += x;
    sum_y += y;
    sum_xy += x*y;
    sum_x2 += x*x;
    i++;
  }

  // Calculating regression line
  b = (period*sum_xy - sum_x*sum_y)/(period*sum_x2 - sum_x*sum_x);
  a = (sum_y - b*sum_x)/period;

  // Calculating channel height
  i = shift;
  max = 0;
  for (x=0; x < period; x++)
  {
    y = a + b*x;
    z = fabs(GetPrice(i, PriceType) - y);
    if (z > max) max = z;
    i++;
  }

  // Returning channel values
  StartValue = a + b*period;
  EndValue = a;
  Height = max;
  Top = a + max;
  Bottom = a - max;
}

//-----Get moving average------------------------------------------------------
double GetMA(int index, int shift, int period, TMAType maType, TPriceType ApplyTo, double prev)
{
  int i;
  double k, sum, weight;

  switch (maType)
  {
    case ma_SMA:
    {
      // SMA
      if ((index+shift+period >= Bars()) || (index+shift < 0)) return 0;

      sum = 0;
      for (i=index; i < index+period; i++)
        sum += GetPrice(i+shift, ApplyTo);
      
      return sum/period;
    }

    case ma_EMA:
    {
      // EMA
      k = 2.0/(period + 1);
      i = index + shift;
      if ((i > Bars() - 1) || (i < 0)) return 0;

      if (prev == 0) 
        return GetPrice(i, ApplyTo);
      else
        return (prev + k*(GetPrice(i, ApplyTo) - prev));
    }

    case ma_WMA:
    {
      // WMA
      if ((index+shift+period >= Bars()) || (index+shift < 0)) return 0;

      sum = 0;
      weight = 0;
      for (i=0; i < period; i++)
      {
        sum += GetPrice(index+i+shift, ApplyTo)*(period-i);
        weight += (period-i);
      }
      
      return sum/weight;
    }

    default:
    {
      // SSMA
      if ((index+shift+period >= Bars()) || (index+shift < 0)) return 0;

      if (prev == 0)
         return GetMA(index, shift, period, ma_SMA, ApplyTo, prev);
      else
         return (prev*period - prev + GetPrice(index + shift, ApplyTo))/period;
    }
  } //switch
}

// -------- GetMA Implementation based on TIndexBuffer --------------------------------------------
double GetMA(TIndexBuffer priceBars, int index, int shift, int period, TMAType maType, double prev)
{
  int i;
  double k, sum, weight;

  switch (maType)
  {
    case ma_SMA:
    {
      // SMA
      if ((index+shift+period >= Bars()) || (index+shift < 0)) return 0;

      sum = 0;
      for (i=index; i < index+period; i++)
        sum += priceBars[i+shift];
      
      return sum/period;
    }

    case ma_EMA:
    {
      // EMA
      k = 2.0/(period + 1);
      i = index + shift;
      if ((i > Bars() - 1) || (i < 0)) return 0;

      if (prev == 0) 
        return priceBars[i];
      else
        return (prev + k*(priceBars[i] - prev));
    }

    case ma_WMA:
    {
      // WMA
      if ((index+shift+period >= Bars()) || (index+shift < 0)) return 0;

      sum = 0;
      weight = 0;
      for (i=0; i < period; i++)
      {
        sum += priceBars[index+i+shift]*(period-i);
        weight += (period-i);
      }
      
      return sum/weight;
    }

    default:
    {
      // SSMA
      if ((index+shift+period >= Bars()) || (index+shift < 0)) return 0;

      if (prev == 0)
		return GetMA(priceBars,index,shift,period,ma_SMA,prev);
      else
         return (prev*period - prev + priceBars[index + shift])/period;
    }
  } //switch
}

//-----Get moving average based on time frame ----------------------------------
double iGetMA(PChar Symbol, int TimeFrame, int index,
                 int shift, int period, TMAType maType, 
				 TPriceType ApplyTo,double prev)
{

	if( (index + period >= iBars(Symbol, TimeFrame)) || (period < 1) )
		return 0.0;

	switch( maType )
	{
		case ma_SMA: 
		{
			// SMA
			if (index + shift + period >= iBars(Symbol, TimeFrame)
                   || (index + shift < 0)){
				return 0.0;
			} else {
				double sum = 0.0;
				for(int i=index; i<= index + period - 1;i++){
					sum += iGetPrice(Symbol, TimeFrame, i + shift, ApplyTo);
				} // EndFor
				return( sum/period );
			} // EndIf Else
		} // EndCase ma_SMA
		 			 			 			 
		case ma_EMA: 
		{
			// EMA
			double k = 2.0/(period + 1.0);
			int i = index + shift;
			if( (i > iBars(Symbol, TimeFrame) - 1) || (i < 0)){
				return 0.0;
			} else {
				if(prev == 0 ) {
					return( iGetPrice(Symbol, TimeFrame,i, ApplyTo) );
				} else {
					return( prev + k*(iGetPrice(Symbol, TimeFrame, i, ApplyTo)
                              - prev));
				} // EndIfElse prev == 0
			} // EndIf enough bars
		 }// EndCase
		case ma_WMA:
		{
			// WMA
			if( (index + shift + period >= iBars(Symbol, TimeFrame))
				       || (index + shift < 0) ){
			return 0.0;
			} else {
				double sum = 0.0;
				double weight = 0.0;
				for(int i=0; i<= period - 1; i++){
					sum +=iGetPrice(Symbol, TimeFrame, index + i + shift,
						                            ApplyTo)*(period - i);
					weight += (period - i);
				} // EndFor loop
				return( sum/weight );
			} // EndIfElse
		} // EndCase ma_WMA

		default:
		{
			// SSMA
			if( (index + shift + period >= iBars(Symbol, TimeFrame))
				      || (index + shift < 0) ){
				return 0.0;
			} else {
				if( prev == 0 ){
					return iGetMA( Symbol, TimeFrame,index, shift, period, ma_SMA,
                                         ApplyTo, prev);
				} else {
					return (prev*period - prev + iGetPrice(Symbol, TimeFrame,
                                        index + shift, ApplyTo))/period;
				} //EndIfElse
			} //EndIfElse enough bars
		} // EndCase default
	} // EndSwitch
} // EndFunction


double _value(TValueType ValueType, int index)
{
  switch (ValueType)
  {
    case vt_Open:  return Open(index);
    case vt_High:  return High(index);
    case vt_Low:   return Low(index);
    case vt_Close: return Close(index);
    default:       return Volume(index);
  }
}

//-----Get highest value-------------------------------------------------------
double GetHighestValue(TValueType ValueType, int StartIndex, int count)
{

  int i;
  double v;

  double result = _value(ValueType, StartIndex);
  for (i=1; i < count; i++)
  {
    v = _value(ValueType, StartIndex + i);
    if (v > result) result = v;
  }

  return result;
}

//-----Get lowest value--------------------------------------------------------
double GetLowestValue(TValueType ValueType, int StartIndex, int count)
{
  int i;
  double v;

  double result = _value(ValueType, StartIndex);
  for (i=1; i < count; i++)
  {
    v = _value(ValueType, StartIndex + i);
    if (v < result) result = v;
  }

  return result;
}
