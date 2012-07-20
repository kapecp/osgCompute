/* osgCompute - Copyright (C) 2008-2009 SVT Group
*                                                                     
* This library is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation; either version 3 of
* the License, or (at your option) any later version.
*                                                                     
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of 
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesse General Public License for more details.
*
* The full license is in LICENSE file included with this distribution.
*/

#include <sstream>
#include <osg/NodeVisitor>
#include <osg/OperationThread>
#include <osgDB/Registry>
#include <osgUtil/CullVisitor>
//#include <osgUtil/RenderBin>
#include <osgUtil/RenderStage>
#include <osgUtil/GLObjectsVisitor>
#include <osgCompute/Visitor>
#include <osgCompute/Memory>
#include <osgCompute/Program>

namespace osgCompute
{
    class LIBRARY_EXPORT ProgramBin : public osgUtil::RenderStage 
    {
    public:
        ProgramBin(); 
        ProgramBin(osgUtil::RenderBin::SortMode mode);

        META_Object( osgCompute, ProgramBin );

        virtual bool setup( Program& program );
        virtual void reset();

        virtual void draw( osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous );
        virtual void drawImplementation(osg::RenderInfo& renderInfo,osgUtil::RenderLeaf*& previous);
        virtual void drawInner(osg::RenderInfo& renderInfo,osgUtil::RenderLeaf*& previous, bool& doCopyTexture);

        virtual unsigned int computeNumberOfDynamicRenderLeaves() const;

        virtual Program* getProgram();
        virtual const Program* getProgram() const;

    protected:
        friend class Program;
        virtual ~ProgramBin() { clearLocal(); }
        void clearLocal();

        virtual void launch();
        virtual void drawLeafs( osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous );

        Program* _program; 

    private:
        // copy constructor and operator should not be called
        ProgramBin(const ProgramBin&, const osg::CopyOp& ) {}
        ProgramBin &operator=(const ProgramBin &) { return *this; }
    };

    /////////////////////////////////////////////////////////////////////////////////////////////////
    // PUBLIC FUNCTIONS /////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////
    //------------------------------------------------------------------------------
    ProgramBin::ProgramBin()
        : osgUtil::RenderStage()
    {
        clearLocal();
    }

    //------------------------------------------------------------------------------
    ProgramBin::ProgramBin( osgUtil::RenderBin::SortMode mode )
        : osgUtil::RenderStage( mode )
    {
        clearLocal();
    }

    //------------------------------------------------------------------------------
    void ProgramBin::drawInner( osg::RenderInfo& renderInfo,osgUtil::RenderLeaf*& previous, bool& doCopyTexture )
    {
        osgUtil::RenderBin::draw(renderInfo,previous);
    }

    //------------------------------------------------------------------------------
    void ProgramBin::draw( osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous )
    { 
        if (_stageDrawnThisFrame) return;
        _stageDrawnThisFrame = true;

        // Render all the pre draw callbacks
        drawPreRenderStages(renderInfo,previous);

        bool doCopyTexture = false;
        // Draw bins, renderleafs and launch kernels
        drawInner(renderInfo,previous,doCopyTexture);

        // Render all the post draw callbacks
        drawPostRenderStages(renderInfo,previous);
    }

    //------------------------------------------------------------------------------
    void ProgramBin::drawImplementation( osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous )
    { 
        osg::State& state = *renderInfo.getState();

        unsigned int numToPop = (previous ? osgUtil::StateGraph::numToPop(previous->_parent) : 0);
        if (numToPop>1) --numToPop;
        unsigned int insertStateSetPosition = state.getStateSetStackSize() - numToPop;

        if (_stateset.valid())
        {
            state.insertStateSet(insertStateSetPosition, _stateset.get());
        }

        ///////////////////
        // DRAW PRE BINS //
        ///////////////////
        osgUtil::RenderBin::RenderBinList::iterator rbitr;
        for(rbitr = _bins.begin();
            rbitr!=_bins.end() && rbitr->first<0;
            ++rbitr)
        {
            rbitr->second->draw(renderInfo,previous);
        }

        if( _program->getLaunchCallback() ) 
            (*_program->getLaunchCallback())( *_program ); 
        else launch();  

        // don't forget to decrement dynamic object count
        renderInfo.getState()->decrementDynamicObjectCount();

        ////////////////////
        // DRAW POST BINS //
        ////////////////////
        for(;
            rbitr!=_bins.end();
            ++rbitr)
        {
            rbitr->second->draw(renderInfo,previous);
        }


        if (_stateset.valid())
        {
            state.removeStateSet(insertStateSetPosition);
        }
    }

    //------------------------------------------------------------------------------
    unsigned int ProgramBin::computeNumberOfDynamicRenderLeaves() const
    {
        // increment dynamic object count to execute computations
        return osgUtil::RenderBin::computeNumberOfDynamicRenderLeaves() + 1;
    }

    //------------------------------------------------------------------------------
    bool ProgramBin::setup( Program& program )
    {
        // COMPUTATION 
        _program = &program;

        // OBJECT 
        setName( _program->getName() );
        setDataVariance( _program->getDataVariance() );

        return true;
    }

    //------------------------------------------------------------------------------
    void ProgramBin::reset()
    {
        clearLocal();
    }

    //------------------------------------------------------------------------------
    Program* ProgramBin::getProgram()
    { 
        return _program; 
    }

    //------------------------------------------------------------------------------
    const Program* ProgramBin::getProgram() const
    { 
        return _program; 
    }


    /////////////////////////////////////////////////////////////////////////////////////////////////
    // PROTECTED FUNCTIONS //////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////
    //------------------------------------------------------------------------------
    void ProgramBin::clearLocal()
    {
        _stageDrawnThisFrame = false;
        _program = NULL;
        osgUtil::RenderStage::reset();
    }

    //------------------------------------------------------------------------------
    void ProgramBin::launch()
    {
        if( !_program  )
            return;

        // Launch computations
        ComputationList& computations = _program->getComputations();
        for( ComputationListCnstItr itr = computations.begin(); itr != computations.end(); ++itr )
        {
            if( (*itr)->isEnabled() )
            {
                (*itr)->launch();
            }
        }
    }

    //------------------------------------------------------------------------------
    void ProgramBin::drawLeafs( osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous )
    {
        // draw fine grained ordering.
        for(osgUtil::RenderBin::RenderLeafList::iterator rlitr= _renderLeafList.begin();
            rlitr!= _renderLeafList.end();
            ++rlitr)
        {
            osgUtil::RenderLeaf* rl = *rlitr;
            rl->render(renderInfo,previous);
            previous = rl;
        }

        // draw coarse grained ordering.
        for(osgUtil::RenderBin::StateGraphList::iterator oitr=_stateGraphList.begin();
            oitr!=_stateGraphList.end();
            ++oitr)
        {

            for(osgUtil::StateGraph::LeafList::iterator dw_itr = (*oitr)->_leaves.begin();
                dw_itr != (*oitr)->_leaves.end();
                ++dw_itr)
            {
                osgUtil::RenderLeaf* rl = dw_itr->get();
                rl->render(renderInfo,previous);
                previous = rl;

            }
        }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////
    // PUBLIC FUNCTIONS /////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////
    //------------------------------------------------------------------------------ 
    Program::Program() 
        :   osg::Group()
    { 
        _launchCallback = NULL;
        _enabled = true;

        // setup program order
        _computeOrder = UPDATE_BEFORECHILDREN;
        if( (_computeOrder & OSGCOMPUTE_UPDATE) == OSGCOMPUTE_UPDATE )
            setNumChildrenRequiringUpdateTraversal( 1 );
    }

    //------------------------------------------------------------------------------
    void Program::accept(osg::NodeVisitor& nv) 
    { 
        if( nv.validNodeMask(*this) ) 
        {  
            nv.pushOntoNodePath(this);

            osgUtil::CullVisitor* cv = dynamic_cast<osgUtil::CullVisitor*>( &nv );
            if( cv != NULL )
            {
                if( _enabled && (_computeOrder & OSGCOMPUTE_RENDER) == OSGCOMPUTE_RENDER )
                    addBin( *cv );
                else if( (_computeOrder & OSGCOMPUTE_NORENDER) == OSGCOMPUTE_NORENDER )
					return; // Do not process the childs during rendering
				else
                    nv.apply(*this);
            }
            else if( nv.getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR )
            {

                if( _enabled && (_computeOrder & UPDATE_BEFORECHILDREN) == UPDATE_BEFORECHILDREN )
                    launch();

                if( getUpdateCallback() )
                {
                    (*getUpdateCallback())( this, &nv );
                }
                else
                {
                    applyVisitorToComputations( nv );
                    nv.apply( *this );
                }

                if( _enabled && (_computeOrder & UPDATE_AFTERCHILDREN) == UPDATE_AFTERCHILDREN )
                    launch();
            }
            else if( nv.getVisitorType() == osg::NodeVisitor::EVENT_VISITOR )
            {
                if( getEventCallback() )
                {
                    (*getEventCallback())( this, &nv );
                }
                else
                {
                    applyVisitorToComputations( nv );
                    nv.apply( *this );
                }
            }
            else
            {
                nv.apply( *this );
            }

            nv.popFromNodePath(); 
        } 
    }

    //------------------------------------------------------------------------------
    void Program::addComputation( Computation& computation )
    {
        Resource* curResource = NULL;
        for( ResourceHandleListItr itr = _resources.begin(); itr != _resources.end(); ++itr )
        {
            curResource = (*itr)._resource.get();
            if( !curResource )
                continue;

            computation.acceptResource( *curResource );
        }

        // increment traversal counter if required
        if( computation.getEventCallback() )
            osg::Node::setNumChildrenRequiringEventTraversal( osg::Node::getNumChildrenRequiringEventTraversal() + 1 );

        if( computation.getUpdateCallback() )
            osg::Node::setNumChildrenRequiringUpdateTraversal( osg::Node::getNumChildrenRequiringUpdateTraversal() + 1 );


        _computations.push_back( &computation );
    }

    //------------------------------------------------------------------------------
    void Program::removeComputation( Computation& computation )
    {
        for( ComputationListItr itr = _computations.begin(); itr != _computations.end(); ++itr )
        {
            if( (*itr) == &computation )
            {
                // decrement traversal counter if necessary
                if( computation.getEventCallback() )
                    osg::Node::setNumChildrenRequiringEventTraversal( osg::Node::getNumChildrenRequiringEventTraversal() - 1 );

                if( computation.getUpdateCallback() )
                    osg::Node::setNumChildrenRequiringUpdateTraversal( osg::Node::getNumChildrenRequiringUpdateTraversal() - 1 );

                _computations.erase( itr );
                return;
            }
        }
    }

    //------------------------------------------------------------------------------
    void Program::removeComputation( const std::string& computationIdentifier )
    {
        ComputationListItr itr = _computations.begin();
        while( itr != _computations.end() )
        {
            if( (*itr)->isIdentifiedBy( computationIdentifier ) )
            {
                // decrement traversal counter if necessary
                if( (*itr)->getEventCallback() )
                    osg::Node::setNumChildrenRequiringEventTraversal( osg::Node::getNumChildrenRequiringEventTraversal() - 1 );

                if( (*itr)->getUpdateCallback() )
                    osg::Node::setNumChildrenRequiringUpdateTraversal( osg::Node::getNumChildrenRequiringUpdateTraversal() - 1 );


                _computations.erase( itr );
                itr = _computations.begin();
            }
            else
            {
                ++itr;
            }
        }
    }

    //------------------------------------------------------------------------------
    void Program::removeComputations()
    {
        ComputationListItr itr;
        while( !_computations.empty() )
        {
            itr = _computations.begin();

            Computation* curComputation = (*itr).get();
            if( curComputation != NULL )
            {
                // decrement traversal counter if necessary
                if( curComputation->getEventCallback() )
                    osg::Node::setNumChildrenRequiringEventTraversal( osg::Node::getNumChildrenRequiringEventTraversal() - 1 );

                if( curComputation->getUpdateCallback() )
                    osg::Node::setNumChildrenRequiringUpdateTraversal( osg::Node::getNumChildrenRequiringUpdateTraversal() - 1 );
            }
            _computations.erase( itr );
        }
    }

	//------------------------------------------------------------------------------
	const Computation* Program::getComputation( const std::string& computationIdentifier ) const
	{
		for( ComputationListCnstItr itr = _computations.begin(); itr != _computations.end(); ++itr )
			if( (*itr)->isIdentifiedBy( computationIdentifier ) )
				return (*itr).get();

		return NULL;
	}

	//------------------------------------------------------------------------------
	Computation* Program::getComputation( const std::string& computationIdentifier )
	{
		for( ComputationListItr itr = _computations.begin(); itr != _computations.end(); ++itr )
			if( (*itr)->isIdentifiedBy( computationIdentifier ) )
				return (*itr).get();

		return NULL;
	}


    //------------------------------------------------------------------------------
    bool Program::hasComputation( const std::string& computationIdentifier ) const
    {
        for( ComputationListCnstItr itr = _computations.begin(); itr != _computations.end(); ++itr )
            if( (*itr)->isIdentifiedBy( computationIdentifier ) )
                return true;

        return false;
    }

    //------------------------------------------------------------------------------
    bool Program::hasComputation( Computation& computation ) const
    {
        for( ComputationListCnstItr itr = _computations.begin(); itr != _computations.end(); ++itr )
            if( (*itr) == &computation )
                return true;

        return false;
    }

    //------------------------------------------------------------------------------
    bool Program::hasComputations() const 
    { 
        return !_computations.empty(); 
    }

    //------------------------------------------------------------------------------
    ComputationList& Program::getComputations() 
    { 
        return _computations; 
    }

    //------------------------------------------------------------------------------
    const ComputationList& Program::getComputations() const 
    { 
        return _computations; 
    }

    //------------------------------------------------------------------------------
    unsigned int Program::getNumComputations() const 
    { 
        return _computations.size(); 
    }

    //------------------------------------------------------------------------------
    bool osgCompute::Program::hasResource( Resource& resource ) const
    {
		for( ResourceHandleListCnstItr itr = _resources.begin();
			itr != _resources.end();
			++itr )
		{
			if( (*itr)._resource == &resource )
				return true;
		}

        return false;
    }

    //------------------------------------------------------------------------------
    bool osgCompute::Program::hasResource( const std::string& handle ) const
    {
        for( ResourceHandleListCnstItr itr = _resources.begin(); itr != _resources.end(); ++itr )
        {
            if(	(*itr)._resource.valid() && (*itr)._resource->isIdentifiedBy(handle)  )
                return true;
        }

        return false;
    }

    //------------------------------------------------------------------------------
    void osgCompute::Program::addResource( Resource& resource, bool serialize )
    {
        if( hasResource(resource) )
            return;

        for( ComputationListItr itr = _computations.begin(); itr != _computations.end(); ++itr )
            (*itr)->acceptResource( resource );

		ResourceHandle newHandle;
		newHandle._resource = &resource;
		newHandle._serialize = serialize;
		_resources.push_back( newHandle );
    }

    //------------------------------------------------------------------------------
    void Program::exchangeResource( Resource& newResource, bool serialize /*= true */ )
    {
        IdentifierSet& ids = newResource.getIdentifiers();
        for( ResourceHandleListItr itr = _resources.begin(); itr != _resources.end(); ++itr )
        {
            bool exchange = false;
            for( IdentifierSetItr idItr = ids.begin(); idItr != ids.end(); ++idItr )
            {
                if( (*itr)._resource->isIdentifiedBy( (*idItr) ) )
                {
                    exchange = true;
                }
            }

            if( exchange )
            {
                // Remove and add resource
                for( ComputationListItr modItr = _computations.begin(); modItr != _computations.end(); ++modItr )
                {
                    (*modItr)->removeResource( *((*itr)._resource) );
                    (*modItr)->acceptResource( newResource );
                }
                // Remove resource from list
                itr = _resources.erase( itr );
            }
        }

        // Add new resource
        ResourceHandle newHandle;
        newHandle._resource = &newResource;
        newHandle._serialize = serialize;
        _resources.push_back( newHandle );
    }


    //------------------------------------------------------------------------------
    void osgCompute::Program::removeResource( const std::string& handle )
    {
        Resource* curResource = NULL;

        ResourceHandleListItr itr = _resources.begin();
        while( itr != _resources.end() )
        {
            curResource = (*itr)._resource.get();
            if( !curResource )
                continue;

            if( curResource->isIdentifiedBy( handle ) )
            {
                for( ComputationListItr moditr = _computations.begin(); moditr != _computations.end(); ++moditr )
                    (*moditr)->removeResource( *curResource );

                _resources.erase( itr );
                itr = _resources.begin();
            }
            else
            {
                ++itr;
            }
        }
    }

    //------------------------------------------------------------------------------
    void Program::removeResource( Resource& resource )
    {
		for( ResourceHandleListItr itr = _resources.begin();
			itr != _resources.end();
			++itr )
		{
			if( (*itr)._resource == &resource )
			{
				for( ComputationListItr moditr = _computations.begin(); moditr != _computations.end(); ++moditr )
					(*moditr)->removeResource( resource );

				_resources.erase( itr );
				return;
			}
		}

    }

    //------------------------------------------------------------------------------
    void Program::removeResources()
    {
        ResourceHandleListItr itr = _resources.begin();
        while( itr != _resources.end() )
        {
            Resource* curResource = (*itr)._resource.get();
            if( curResource != NULL )
            {
                for( ComputationListItr moditr = _computations.begin(); moditr != _computations.end(); ++moditr )
                    (*moditr)->removeResource( *curResource );
            }

            _resources.erase( itr );
            itr = _resources.begin();
        }
    }

    //------------------------------------------------------------------------------
    ResourceHandleList& Program::getResources()
    {
        return _resources;
    }

    //------------------------------------------------------------------------------
    const ResourceHandleList& Program::getResources() const
    {
        return _resources;
    }

	//------------------------------------------------------------------------------
	bool Program::isResourceSerialized( Resource& resource ) const
	{
		for( ResourceHandleListCnstItr itr = _resources.begin();
			itr != _resources.end();
			++itr )
		{
			if( (*itr)._resource == &resource )
				return (*itr)._serialize;
		}

		return false;
	}

    /////////////////////////////////////////////////////////////////////////////////////////////////
    // PUBLIC FUNCTIONS /////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////
    //------------------------------------------------------------------------------
    void Program::setLaunchCallback( LaunchCallback* lc ) 
    { 
        if( lc == _launchCallback )
            return;

        _launchCallback = lc; 
    }

    //------------------------------------------------------------------------------
    LaunchCallback* Program::getLaunchCallback() 
    { 
        return _launchCallback; 
    }

    //------------------------------------------------------------------------------
    const LaunchCallback* Program::getLaunchCallback() const 
    { 
        return _launchCallback; 
    }

    //------------------------------------------------------------------------------
    void Program::setComputeOrder( Program::ComputeOrder co, int orderNum/* = 0 */)
    {
        // deactivate auto update
        if( (_computeOrder & OSGCOMPUTE_UPDATE ) == OSGCOMPUTE_UPDATE )
            setNumChildrenRequiringUpdateTraversal( getNumChildrenRequiringUpdateTraversal() - 1 );

        _computeOrder = co;
        _computeOrderNum = orderNum;

        // set auto update active in case we use the update traversal to compute things
        if( (_computeOrder & OSGCOMPUTE_UPDATE ) == OSGCOMPUTE_UPDATE )
            setNumChildrenRequiringUpdateTraversal( getNumChildrenRequiringUpdateTraversal() + 1 );
    }

    //------------------------------------------------------------------------------
    Program::ComputeOrder Program::getComputeOrder() const
    {
        return _computeOrder;
    }

    //------------------------------------------------------------------------------
    void Program::enable() 
    { 
        _enabled = true; 
    }

    //------------------------------------------------------------------------------
    void Program::disable() 
    { 
        _enabled = false; 
    }

    //------------------------------------------------------------------------------
    bool Program::isEnabled() const
    {
        return _enabled;
    }

    //------------------------------------------------------------------------------
    void Program::releaseGLObjects( osg::State* state ) const
    {
        if( state != NULL && GLMemory::getContext() == state->getGraphicsContext() )
        {
            // Make context the current context
            if( !GLMemory::getContext()->isCurrent() && GLMemory::getContext()->isRealized() )
                GLMemory::getContext()->makeCurrent();

            // Release all resources associated with the current context
            for( ComputationListItr itr = _computations.begin(); itr != _computations.end(); ++itr )
                (*itr)->releaseObjects();

            for( ResourceHandleListItr itr = _resources.begin(); itr != _resources.end(); ++itr )
                (*itr)._resource->releaseObjects();
        }

        Group::releaseGLObjects( state );
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////
    // PROTECTED FUNCTIONS //////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////
    //------------------------------------------------------------------------------
    void Program::addBin( osgUtil::CullVisitor& cv )
    {
        if( !cv.getState() )
        {
            osg::notify(osg::WARN)  << "Program::addBin() for \""
                << getName()<<"\": CullVisitor must provide a valid state."
                << std::endl;

            return;
        }

        if( GLMemory::getContext() == NULL )
            return;

        if( cv.getState()->getContextID() != GLMemory::getContext()->getState()->getContextID() )
            return;

        ///////////////////////
        // SETUP REDIRECTION //
        ///////////////////////
        osgUtil::RenderBin* oldRB = cv.getCurrentRenderBin();
        if( !oldRB )
        {
            osg::notify(osg::WARN)  
                << getName() << " [Program::addBin()]: current CullVisitor has no active RenderBin."
                << std::endl;

            return;
        }

        ProgramBin* pb = new ProgramBin;
        if( !pb )
        {
            osg::notify(osg::FATAL)  
                << getName() << " [Program::addBin()]: cannot create ProgramBin."
                << std::endl;

            return;
        }
        pb->setup( *this );

        //////////////
        // TRAVERSE //
        //////////////
        cv.setCurrentRenderBin( pb );
        cv.apply( *this );
        cv.setCurrentRenderBin( oldRB );

        osgUtil::RenderStage* rs = oldRB->getStage();
        if( rs )
        {
            if( (_computeOrder & OSGCOMPUTE_POSTRENDER) ==  OSGCOMPUTE_POSTRENDER )
            {
                rs->addPostRenderStage(pb,_computeOrderNum);
            }
            else
            {
                rs->addPreRenderStage(pb,_computeOrderNum);
            }
        }
    }

    //------------------------------------------------------------------------------
    void Program::launch()
    {            
        // Check if graphics context exist
        // or return otherwise
        if( NULL != GLMemory::getContext() && GLMemory::getContext()->isRealized() )
        {       
            // Launch computations
            if( _launchCallback.valid() ) 
            {
                (*_launchCallback)( *this ); 
            }
            else
            {
                for( ComputationListItr itr = _computations.begin(); itr != _computations.end(); ++itr )
                {
                    if( (*itr)->isEnabled() )
                    {
                        (*itr)->launch();
                    }
                }
            }
        }
    }

    //------------------------------------------------------------------------------
    void Program::applyVisitorToComputations( osg::NodeVisitor& nv )
    {
        if( nv.getVisitorType() == osg::NodeVisitor::EVENT_VISITOR )
        {
            for( ComputationListItr itr = _computations.begin(); itr != _computations.end(); ++itr )
            {
                if( (*itr)->getEventCallback() )
                    (*(*itr)->getEventCallback())( *(*itr), nv );
            }
        }
        else if( nv.getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR )
        {
            for( ComputationListItr itr = _computations.begin(); itr != _computations.end(); ++itr )
            {
                if( (*itr)->getUpdateCallback() )
                    (*(*itr)->getUpdateCallback())( *(*itr), nv );
            }
        }
    }  
} 