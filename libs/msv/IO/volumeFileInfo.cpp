
#include "volumeFileInfo.h"

#include <msv/tree/volumeTreeTensor.h>
#include <msv/tree/volumeTreeBase.h>

#include <msv/IO/dataHDDIORaw.h>
#include <msv/IO/dataHDDIOOctree.h>
#include <msv/IO/dataHDDIOTensor.h>

#include <msv/util/debug.h>
#include <msv/util/str.h>
#include <msv/util/fileIO.h>

#include <iostream>
#include <fstream>
#include <map>

namespace massVolVis
{

VolumeFileInfo::VolumeFileInfo()
    :_attributes( 0 )
{
    reset();
}


VolumeFileInfo::VolumeFileInfo( const VolumeFileInfo& fileInfo )
{
    *this = fileInfo;
}


VolumeFileInfo::~VolumeFileInfo()
{
}


DataHDDIOSPtr VolumeFileInfo::createDataHDDIO( bool initTree )
{
    if( !isAttributeSet( COMPRESSION ) || _compression == NONE )
    {

        if( !isAttributeSet( BLOCK_DIM ) )
            return DataHDDIOSPtr( new DataHDDIORaw( *this ));

        return DataHDDIOSPtr( new DataHDDIOOctree( *this ));
    }

    DataHDDIOSPtr tensorIO;
    switch( _compression )
    {
        case TENSOR:
            tensorIO = DataHDDIOSPtr( new DataHDDIOTensorBasic( *this, initTree ));
            break;

        case TENSOR_QUANTIZED:
            tensorIO = DataHDDIOSPtr( new DataHDDIOTensorQuantized( *this, initTree ));
            break;

        case TENSOR_QUANTIZED_2G:
            tensorIO = DataHDDIOSPtr( new DataHDDIOTensorQuantized2G( *this, initTree ));
            break;

        case TENSOR_QUANTIZED_ERRORS_2G:
            tensorIO = DataHDDIOSPtr( new DataHDDIOTensorQuantizedErrors2G( *this, initTree ));
            break;

        default:
            LOG_ERROR << " Specified compression is not supported " << std::endl;
            return DataHDDIOSPtr();
    }

    if( tensorIO->getMaxRankDim() != getMaxRankDim() )
        setMaxRankDim( tensorIO->getMaxRankDim() );

    return tensorIO;
}


void VolumeFileInfo::reset()
{
    setVersion(   0 );
    setSourceDims( Vec3_ui16() );
    setU123SrcDims(Vec2_ui16() );
    setUOffsets( "" );
    setBlockDim(  0 );
    setMaxRankDim(0 );
    setBorderDim( 0 );
    setBytesNum(  0 );
    setCompression( NONE );
    setDataFileName( "" );
    setTFFileName( "" );

    _attributes     = 0;
}


void VolumeFileInfo::setDefaults()
{
    setVersion(   1 );
    setBytesNum(  1 );
    setBorderDim( 0 );
    setCompression( NONE );
}


void VolumeFileInfo::setDataFileName( const std::string& fileName )
{
    _dataFileName =  fileName;
    _dataFileDir  = (fileName == "") ? "" : util::getDirName( fileName );
    _setAttribute( DATA_FILE );
}


void VolumeFileInfo::_setAttribute( const DataType dataType )
{
    _attributes |= dataType;
}


bool VolumeFileInfo::isAttributeSet( const DataType dataType ) const
{
    return _attributes & dataType;
}


uint32_t VolumeFileInfo::getBlockSize_() const
{
    if( _compression == TENSOR )
    {
        const uint32_t r = getMaxRankDim();
        return r*r*r*sizeof(float);
    }
    if( _compression == TENSOR_QUANTIZED ||
        _compression == TENSOR_QUANTIZED_2G ||
        _compression == TENSOR_QUANTIZED_ERRORS_2G )
    {
        const uint32_t r = getMaxRankDim();
        return r*r*r*sizeof(byte);
    }
    //else no compression
    uint32_t bs = _blockDim + _border*2;
    bs = bs*bs*bs*_bytes;

    return bs;
}


bool VolumeFileInfo::load( const std::string& file )
{
    reset();

    // check that we have ".dat" file
    size_t pos = file.find_last_of( '.' );
    std::string extention = file.substr( pos + 1 );
    if( extention != "dat" )
    {
        LOG_ERROR << "Only .dat files are accepted" << std::endl;
        return false;
    }

    // open the file and read duplets "str: str" to the std::map
    std::ifstream inFile( file.c_str(), std::ios::in );
    if( !inFile.is_open() || !inFile.good() || inFile.eof() )
    {
        LOG_ERROR << "Can't open file " << file << " for reading or file is empty" << std::endl;
        return false;
    }

    typedef std::map< std::string, std::string > SSMap;

    SSMap values;

    while( inFile.good() && !inFile.eof() )
    {
        std::string line, value;

        std::getline( inFile, line );

        if( line.size() == 0 || line[0] == '#' ) // skip comments
            continue;

        pos = line.find_first_of( ':' );
        if( pos != std::string::npos )
        {
            value = line.substr( pos + 1 );
            values[ strUtil::trim( line.substr( 0, pos )).c_str() ] = strUtil::trim( value );
        }else
        {
            if( strUtil::trim( line ) == "UseDefault" )
                setDefaults();
        }
    }
    inFile.close();

    pos = file.find_last_of( '/' );

    const std::string dir = ( pos == std::string::npos ) ? "" : file.substr( 0, pos + 1 );

    SSMap::iterator it;
    if(( it = values.find( "ObjectFileName" )) != values.end() )
    {
        std::string fName = it->second;
        if( fName.size() > 0 )
        {
            if( fName[0] == '/' )
                _dataFileName = it->second;
            else
                _dataFileName = dir + it->second;
            _dataFileDir = util::getDirName( _dataFileName );
            _setAttribute( DATA_FILE );
        }
    }

    if(( it = values.find( "TFFileName" )) != values.end() )
    {
        std::string fName = it->second;
        if( fName.size() > 0 )
        {
            if( fName[0] == '/' )
                _tfFileName = it->second;
            else
                _tfFileName = dir + it->second;
            _setAttribute( TF_FILE );
        }
    }

    if(( it = values.find( "UOffsets" )) != values.end() )
    {
        _uOffsets = it->second;
        _setAttribute( U_OFFSETS );
    }

    if(( it = values.find( "Version" )) != values.end() )
    {
        std::stringstream ss( it->second );
        ss >> _version;
        _setAttribute( VERSION );
    }

    if(( it = values.find( "SrcSize" )) != values.end() )
    {
        std::stringstream ss( it->second );
        ss >> _srcDims.w;
        ss >> _srcDims.h;
        ss >> _srcDims.d;
        _setAttribute( SOURCE_DIMS );
    }

    if(( it = values.find( "MaxRankSize" )) != values.end() )
    {
        std::stringstream ss( it->second );
        ss >> _maxRankDim;
        _setAttribute( MAX_RANK );
    }

    if(( it = values.find( "U123SrcSize" )) != values.end() )
    {
        std::stringstream ss( it->second );
        ss >> _u123SrcDims.w;
        ss >> _u123SrcDims.h;
        _setAttribute( U123_DIMS );
    }

    if(( it = values.find( "BlockSize" )) != values.end() )
    {
        std::stringstream ss( it->second );
        ss >> _blockDim;
        _setAttribute( BLOCK_DIM );
    }

    if(( it = values.find( "BorderSize" )) != values.end() )
    {
        std::stringstream ss( it->second );
        int b = 0;
        ss >> b;
        _border = b;
        _setAttribute( BORDER_DIM );
    }

    if(( it = values.find( "Format" )) != values.end() )
    {
        if( it->second == "SHORT" || it->second == "USHORT" )
            _bytes = 2;
        else
            _bytes = 1;
        _setAttribute( BYTES );
    }

    if(( it = values.find( "Compression" )) != values.end() )
    {
        if( it->second == "NONE" )
            _compression = NONE;

        if( it->second == "TENSOR" )
            _compression = TENSOR;

        if( it->second == "TENSOR_QUANTIZED" )
            _compression = TENSOR_QUANTIZED;

        if( it->second == "TENSOR_QUANTIZED_2G" )
            _compression = TENSOR_QUANTIZED_2G;

        if( it->second == "TENSOR_QUANTIZED_ERRORS_2G" )
            _compression = TENSOR_QUANTIZED_ERRORS_2G;

        _setAttribute( COMPRESSION );
    }

    return true;
}


bool VolumeFileInfo::save( const std::string& file ) const
{
    std::ofstream outFile( file.c_str(), std::ios::out | std::ios::trunc );
    if( !outFile.is_open() )
    {
        LOG_ERROR << "Can't open file " << file << " for writing" << std::endl;
        return false;
    }

    std::stringstream ss; 

    if( isAttributeSet( VERSION     )) ss << "Version:          " << _version       << std::endl;
    if( isAttributeSet( DATA_FILE   )) ss << "ObjectFileName:   " << _dataFileName  << std::endl;
    if( isAttributeSet( TF_FILE     )) ss << "TFFileName:       " << _tfFileName    << std::endl;
    if( isAttributeSet( SOURCE_DIMS )) ss << "SrcSize:          " << _srcDims.w << " "
                                                                  << _srcDims.h << " "
                                                                  << _srcDims.d     << std::endl;
    if( isAttributeSet( U123_DIMS   )) ss << "U123SrcSize:      " << _u123SrcDims.w << " "
                                                                  << _u123SrcDims.h << std::endl;
    if( isAttributeSet( U_OFFSETS   )) ss << "UOffsets:         " << _uOffsets      << std::endl;
    if( isAttributeSet( BLOCK_DIM   )) ss << "BlockSize:        " << _blockDim      << std::endl;
    if( isAttributeSet( MAX_RANK    )) ss << "MaxRankSize:      " << _maxRankDim    << std::endl;
    if( isAttributeSet( BORDER_DIM  )) ss << "BorderSize:       " << int(_border)   << std::endl;
    if( isAttributeSet( BYTES       )) ss << "Format:           " << ((_bytes == 1) ? "BYTE" : "SHORT") << std::endl;
    if( isAttributeSet( COMPRESSION ))
    {
        ss << "Compression:      ";
        if( _compression == TENSOR                     ) ss << "TENSOR";              else
        if( _compression == TENSOR_QUANTIZED           ) ss << "TENSOR_QUANTIZED";    else
        if( _compression == TENSOR_QUANTIZED_2G        ) ss << "TENSOR_QUANTIZED_2G"; else
        if( _compression == TENSOR_QUANTIZED_ERRORS_2G ) ss << "TENSOR_QUANTIZED_ERRORS_2G"; else
                                                  ss << "NONE";
        ss << std::endl;
    }

    outFile << ss.str().c_str();
    outFile.close();
    return true;
}


std::ostream& operator<< ( std::ostream& out, const VolumeFileInfo& info )
{
    std::stringstream ss;

    ss << "Octree info {" << std::endl;
    ss                                                << " Attributes:   "          << info._attributes  << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::VERSION     )) ss << "  Version:           " << info._version     << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::DATA_FILE   )) ss << "  Data File:         " << info._dataFileName.c_str() << std::endl
                                                               << "   Dir File:         " << info._dataFileDir.c_str()  << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::TF_FILE     )) ss << "  TF File:           " << info._tfFileName.c_str()   << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::SOURCE_DIMS )) ss << "  Source size:       " << info._srcDims     << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::U123_DIMS   )) ss << "  U1/U2/U3 src size: " << info._u123SrcDims << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::U123_DIMS   )) ss << "  U1/U2/U3 size:     " << info.getU123Dims() << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::U_OFFSETS   )) ss << "  UOffsets:          " << info.getUOffsets() << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::BLOCK_DIM   )) ss << "  Block size:        " << info._blockDim    << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::MAX_RANK    )) ss << "  Max Rank size:     " << info._maxRankDim  << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::BORDER_DIM  )) ss << "  Border size:       " << int(info._border) << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::BYTES       )) ss << "  Bytes size:        " << static_cast<int>( info._bytes ) << std::endl;
    if( info.isAttributeSet( VolumeFileInfo::COMPRESSION ))
    {
        ss << "  Compression: ";
        if( info._compression == VolumeFileInfo::TENSOR                     ) ss << "TENSOR";                     else
        if( info._compression == VolumeFileInfo::TENSOR_QUANTIZED           ) ss << "TENSOR_QUANTIZED";           else
        if( info._compression == VolumeFileInfo::TENSOR_QUANTIZED_2G        ) ss << "TENSOR_QUANTIZED_2G";        else
        if( info._compression == VolumeFileInfo::TENSOR_QUANTIZED_ERRORS_2G ) ss << "TENSOR_QUANTIZED_ERRORS_2G"; else
                                                                       ss << "NONE";

        ss << std::endl;
    }
    ss << "}" << std::endl;

    out << ss.str().c_str();
    return out;
}


}//namespace massVolVis

